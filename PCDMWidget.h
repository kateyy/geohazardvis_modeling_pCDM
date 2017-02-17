#pragma once

#include <QDateTime>
#include <QDockWidget>

#include <memory>

#include "pCDM_types.h"


class QStateMachine;

class DataSetFilter;
class GuiPluginInterface;
class PCDMModel;
class PCDMProject;
class PCDMVisualizationGenerator;
class PCDMWidget_StateHelper;
class Ui_PCDMWidget;


class PCDMWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit PCDMWidget(
        const QString & settingsGroup,
        GuiPluginInterface & pluginInterface,
        QWidget * parent = nullptr,
        Qt::WindowFlags flags = {});
    ~PCDMWidget() override;

private:
    void setupStateMachine();

    void openProjectDialog();
    void newProjectDialog();
    void loadProjectFrom(const QString & rootFolder);

    void prepareSetupSurfaceParameters();
    void cleanupSurfaceParameterSetup();
    void saveSurfaceParameters();
    void updateSurfacePropertiesUi();

    void runModel();
    void showVisualization();

    void sourceParametersToUi(const pCDM::PointCDMParameters & parameters);
    pCDM::PointCDMParameters sourceParametersFromUi() const;

    void updateModelsList();
    //void updateModelsProperties();
    //void deleteSelectedModel();
    //void resetToSelectedModel();
    //void renameSelectedModel();

private:
    const QString m_settingsGroup;
    GuiPluginInterface & m_pluginInterface;

    std::unique_ptr<Ui_PCDMWidget> m_ui;
    std::unique_ptr<QStateMachine> m_stateMachine;
    std::unique_ptr<PCDMWidget_StateHelper> m_stateHelper;
    std::unique_ptr<DataSetFilter> m_coordsDataSetFilter;
    std::unique_ptr<PCDMVisualizationGenerator> m_visGenerator;

    std::unique_ptr<PCDMProject> m_project;
    QDateTime m_lastModelTimestamp;
};
