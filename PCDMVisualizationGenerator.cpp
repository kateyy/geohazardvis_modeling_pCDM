/*
 * GeohazardVis plug-in: pCDM Modeling
 * Copyright (C) 2017 Karsten Tausche <geodev@posteo.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PCDMVisualizationGenerator.h"

#include <array>
#include <cassert>
#include <type_traits>

#include <QCoreApplication>
#include <QDebug>

#include <vtkAOSDataArrayTemplate.h>
#include <vtkImageData.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include <core/AbstractVisualizedData.h>
#include <core/color_mapping/ColorMapping.h>
#include <core/color_mapping/ColorMappingData.h>
#include <core/DataSetHandler.h>
#include <core/data_objects/ImageDataObject.h>
#include <core/data_objects/PointCloudDataObject.h>
#include <core/utility/qthelper.h>
#include <gui/DataMapping.h>
#include <gui/data_view/ResidualVerificationView.h>

#include "pCDM_types.h"
#include "PCDMModel.h"
#include "PCDMProject.h"


using pCDM::t_FP;


namespace
{

const char * const deformationArrayName = { "Modeled Deformation" };
const char * const deformationComponentNames[3] = { "ue", "un", "uv" };

}


PCDMVisualizationGenerator::PCDMVisualizationGenerator(DataMapping & dataMapping)
    : QObject()
    , m_dataMapping{ dataMapping }
{
}

PCDMVisualizationGenerator::~PCDMVisualizationGenerator()
{
    cleanup();

    if (m_renderView && m_renderView->isEmpty())
    {
        m_renderView->close();
    }

    if (m_residualView && m_residualView->isEmpty())
    {
        m_residualView->close();
    }
}

void PCDMVisualizationGenerator::setProject(PCDMProject * project)
{
    if (m_project == project)
    {
        return;
    }

    disconnectAll(m_projectConnections);

    cleanup();

    m_project = project;

    if (!m_project)
    {
        return;
    }

    connect(m_project, &PCDMProject::horizontalCoordinatesChanged,
        this, &PCDMVisualizationGenerator::updateForNewCoordinates);
}

PCDMProject * PCDMVisualizationGenerator::project()
{
    return m_project;
}

void PCDMVisualizationGenerator::openRenderView()
{
    if (m_renderView)
    {
        return;
    }

    m_renderView = m_dataMapping.createDefaultRenderViewType();
}

void PCDMVisualizationGenerator::openResidualView()
{
    if (m_residualView)
    {
        return;
    }

    m_residualView = m_dataMapping.createRenderView<ResidualVerificationView>();
}

DataObject * PCDMVisualizationGenerator::dataObject()
{
    if (m_dataObject)
    {
        return m_dataObject.get();
    }

    auto dataSet = m_project->horizontalCoordinatesDataSet();
    if (!dataSet)
    {
        return{};
    }

    auto visDataSet = vtkSmartPointer<vtkDataSet>::Take(dataSet->NewInstance());
    visDataSet->DeepCopy(dataSet);
    assert(visDataSet->GetPointData()->GetNumberOfArrays() == 0);

    const auto numPoints = dataSet->GetNumberOfPoints();

    auto visArray = vtkSmartPointer<vtkAOSDataArrayTemplate<t_FP>>::New();
    visArray->SetNumberOfComponents(3);
    visArray->SetNumberOfTuples(numPoints);
    visArray->SetName(deformationArrayName);
    visArray->SetComponentName(0, deformationComponentNames[0]);
    visArray->SetComponentName(1, deformationComponentNames[1]);
    visArray->SetComponentName(2, deformationComponentNames[2]);
    visArray->FillValue(0);
    visDataSet->GetPointData()->SetScalars(visArray);

    static const char * dataObjectName = "pCDM Modeling Result";

    if (auto polyData = vtkPolyData::SafeDownCast(visDataSet))
    {
        m_dataObject = std::make_unique<PointCloudDataObject>(dataObjectName, *polyData);
    }
    else if (auto imageData = vtkImageData::SafeDownCast(visDataSet))
    {
        m_dataObject = std::make_unique<ImageDataObject>(dataObjectName, *imageData);
    }
    else
    {
        assert(false);
        qDebug() << "Unsupported data set type for horizontal coordinates: " << visDataSet->GetClassName();
        return{};
    }

    assert(m_dataObject);

    m_dataMapping.dataSetHandler().addExternalData({ m_dataObject.get() });

    return m_dataObject.get();
}

void PCDMVisualizationGenerator::showDataObject()
{
    const auto data = dataObject();
    if (!data)
    {
        return;
    }

    openRenderView();

    QList<DataObject *> incompatible;
    m_renderView->showDataObjects({ m_dataObject.get() }, incompatible);

    if (!incompatible.isEmpty())
    {
        // In case the user "misused" the preview window, create a new view.
        m_renderView = {};
        openRenderView();
        m_renderView->showDataObjects({ m_dataObject.get() }, incompatible);
        assert(incompatible.isEmpty());
    }
}

void PCDMVisualizationGenerator::showDataObjectsInResidualView()
{
    const auto data = dataObject();
    if (!data)
    {
        return;
    }

    openResidualView();

    m_residualView->setModelData(m_dataObject.get());
}

void PCDMVisualizationGenerator::setModel(PCDMModel & model)
{
    if (!m_project || &model.project() != m_project)
    {
        return;
    }

    auto dataSet = m_project->horizontalCoordinatesDataSet();
    if (!dataSet || !dataObject())
    {
        return;
    }
    assert(m_dataObject && dataSet);

    const auto eventDeferral = ScopedEventDeferral(*m_dataObject);

    const auto numPoints = m_dataObject->numberOfPoints();
    auto & pointData = *m_dataObject->dataSet()->GetPointData();
    auto visArray = vtkAOSDataArrayTemplate<t_FP>::FastDownCast(
        pointData.GetAbstractArray(deformationArrayName));
    assert(visArray && visArray->GetNumberOfTuples() == numPoints);

    bool validResults = false;
    while (model.hasResults())
    {
        const auto & uvec = model.results();
        assert(uvec.size() == 3 && uvec[0].size() == uvec[1].size() && uvec[1].size() == uvec[2].size());

        if (numPoints != static_cast<vtkIdType>(uvec[0].size()))
        {
            qDebug() << "Coordinate and uvec result data set have different number of data points.";
            break;
        }

        assert(visArray->GetNumberOfComponents() == static_cast<int>(uvec.size()));

        auto uvecIt = uvec.begin();
        int component = 0;
        for (; uvecIt != uvec.end(); ++uvecIt, ++component)
        {
            for (vtkIdType i = 0; i < numPoints; ++i)
            {
                visArray->SetTypedComponent(i, component, (*uvecIt)[static_cast<size_t>(i)]);
            }
        }
        visArray->Modified();

        validResults = true;
        break;
    }

    if (!validResults)
    {
        visArray->FillValue(static_cast<t_FP>(0));
        visArray->Modified();
    }

    m_dataObject->signal_dataChanged();

    configureVisualizations(validResults);
}

void PCDMVisualizationGenerator::showModel(PCDMModel & model)
{
    if (!m_project || &model.project() != m_project)
    {
        return;
    }

    showDataObject();

    if (!m_renderView)
    {
        return;
    }

    setModel(model);
}

void PCDMVisualizationGenerator::showResidualForModel(PCDMModel & model)
{
    if (!m_project || &model.project() != m_project)
    {
        return;
    }

    showDataObjectsInResidualView();

    if (!m_residualView)
    {
        return;
    }

    setModel(model);
}

void PCDMVisualizationGenerator::cleanup()
{
    if (!m_dataObject)
    {
        return;
    }

    m_dataMapping.removeDataObjects({ m_dataObject.get() });

    m_dataMapping.dataSetHandler().removeExternalData({ m_dataObject.get() });

    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    m_dataObject = {};
}

void PCDMVisualizationGenerator::updateForNewCoordinates()
{
    const bool recreate = m_dataObject != nullptr;

    cleanup();

    if (recreate)
    {
        dataObject();
    }
}

void PCDMVisualizationGenerator::configureVisualizations(bool validResults) const
{
    // Only configure an already shown visualization, don't create a new one.
    if (auto vis = m_renderView ? m_renderView->visualizationFor(m_dataObject.get()) : nullptr)
    {
        // Make sure that one of the result arrays is mapped to colors. If not, switch to the current
        // default array.
        if (validResults &&
            (!vis->colorMapping().isEnabled()
                || vis->colorMapping().currentScalarsName() != deformationArrayName))
        {
            vis->colorMapping().setCurrentScalarsByName(deformationArrayName, true);
            vis->colorMapping().currentScalars().setDataComponent(2);
        }

        // If there are no valid results, make sure that the invalidated/zero values are not mapped.
        if (!validResults && (deformationArrayName == vis->colorMapping().currentScalarsName()))
        {
            vis->colorMapping().setEnabled(false);
        }
    }

    if (auto vis = m_residualView ? m_residualView->visualizationFor(m_dataObject.get(), 1) : nullptr)
    {
        vis->colorMapping().setEnabled(validResults);
        m_residualView->updateResidual();
    }
}
