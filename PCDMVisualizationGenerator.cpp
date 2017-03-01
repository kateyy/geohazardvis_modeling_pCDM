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
#include <gui/data_view/AbstractRenderView.h>

#include "pCDM_types.h"
#include "PCDMModel.h"
#include "PCDMProject.h"


using pCDM::t_FP;


namespace
{

const char * const deformationArrayName = { "Deformation" };

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

void PCDMVisualizationGenerator::showModel(PCDMModel & model)
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

        assert(visArray->GetNumberOfComponents() == uvec.size());

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

    showDataObject();
    if (!m_renderView)
    {
        return;
    }

    auto vis = m_renderView->visualizationFor(m_dataObject.get());
    assert(vis);

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

    m_dataObject->signal_dataChanged();
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
