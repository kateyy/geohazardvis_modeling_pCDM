#pragma once

#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>


class AbstractRenderView;
class DataMapping;
class DataObject;
class PCDMModel;
class PCDMProject;


class PCDMVisualizationGenerator : public QObject
{
    Q_OBJECT

public:
    explicit PCDMVisualizationGenerator(DataMapping & dataMapping);
    ~PCDMVisualizationGenerator() override;

    void setProject(PCDMProject * project);
    PCDMProject * project();

    /**
     * Open a render view that can later be used to visualize modeling results.
     * This class will close the render view only in the destructor. This allows users to use the
     * same view/GUI setup while switching between projects and models.
     */
    void openRenderView();
    /**
     * Create a data object for the horizontal coordinates of the current project.
     * This data object gets deleted when the project coordinates are changed.
     * Point data attribute arrays are initialized. Call showModel() to fill them with modeling
     * result values.
     */
    DataObject * dataObject();
    /**
     * Update the attribute arrays of dataObject() for the selected model.
     * If a render view is opened, this function will add the data object to the view and configure
     * color mappings to visualize "uv" values.
     */
    void showModel(PCDMModel & model);

    void cleanup();

private:
    void updateForNewCoordinates();

private:
    DataMapping & m_dataMapping;
    QPointer<PCDMProject> m_project;

    std::vector<QMetaObject::Connection> m_projectConnections;

    std::unique_ptr<DataObject> m_dataObject;
    QPointer<AbstractRenderView> m_renderView;
};
