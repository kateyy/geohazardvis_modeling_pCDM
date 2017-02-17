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

static const auto uvecArrayNames = { "ue", "un", "uv" };

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
        this, &PCDMVisualizationGenerator::cleanup);
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


    for (auto && arrayName : uvecArrayNames)
    {
        auto array = vtkSmartPointer<vtkAOSDataArrayTemplate<t_FP>>::New();
        array->SetNumberOfValues(numPoints);
        array->SetName(arrayName);
        array->FillValue(0);

        visDataSet->GetPointData()->AddArray(array);
    }

    visDataSet->GetPointData()->SetActiveScalars(*(uvecArrayNames.begin() + 2));


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

void PCDMVisualizationGenerator::showModel(PCDMModel & model)
{
    if (!m_project || &model.project() != m_project)
    {
        return;
    }

    if (!model.hasResults())
    {
        return;
    }

    auto dataSet = m_project->horizontalCoordinatesDataSet();
    if (!dataSet)
    {
        return;
    }

    const auto & uvec = model.results();
    assert(uvec.size() == 3 && uvec[0].size() == uvec[1].size() && uvec[1].size() == uvec[2].size());
    const auto numDataPoints = static_cast<vtkIdType>(uvec[0].size());

    if (dataSet->GetNumberOfPoints() != numDataPoints)
    {
        qDebug() << "Coordinate and uvec result data set have different number of data points.";
        return;
    }

    if (!dataObject())
    {
        return;
    }

    assert(m_dataObject);

    const auto eventDeferral = ScopedEventDeferral(*m_dataObject);

    const auto numPoints = m_dataObject->numberOfPoints();
    auto & pointData = *m_dataObject->dataSet()->GetPointData();

    auto arrayNameIt = uvecArrayNames.begin();
    auto uvecIt = uvec.begin();

    for (; arrayNameIt != uvecArrayNames.end(); ++arrayNameIt, ++uvecIt)
    {
        auto array = pointData.GetArray(*arrayNameIt);
        assert(array && array->GetNumberOfValues() == numPoints);

        for (vtkIdType i = 0; i < numPoints; ++i)
        {
            array->SetComponent(i, 0, (*uvecIt)[static_cast<size_t>(i)]);
        }
        array->Modified();
    }

    qApp->processEvents();
    if (!m_renderView)
    {
        return;
    }

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

    auto vis = m_renderView->visualizationFor(m_dataObject.get());
    assert(vis);

    vis->colorMapping().setCurrentScalarsByName(
        pointData.GetScalars()->GetName(),
        true);

    m_renderView->render();
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
