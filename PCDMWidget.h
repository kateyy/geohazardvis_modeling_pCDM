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

#pragma once

#include <QDateTime>
#include <QDockWidget>
#include <QStringList>

#include <memory>

#include "pCDM_types.h"


class QMenu;
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

protected:
    void showEvent(QShowEvent * event) override;

private:
    void setupStateMachine();

    void loadSettings();
    void saveSettings();
    void prependRecentProject(const QString & projectRootFolder);
    void removeRecentProject(const QString & projectRootFolder);
    void updateRecentProjectsMenu();

    void openProjectDialog();
    void newProjectDialog();
    void loadProjectFrom(const QString & rootFolder);
    void checkLoadProjectFrom(const QString & rootFolder, bool reportError);

    void prepareSetupSurfaceParameters();
    void cleanupSurfaceParameterSetup();
    void saveSurfaceParameters();
    void updateSurfaceSummary();

    void runModel();
    void handleModelDone();
    void saveModelDialog();
    void showVisualization();
    void showResidual();

    void sourceParametersToUi(const pCDM::PointCDMParameters & parameters);
    pCDM::PointCDMParameters sourceParametersFromUi() const;

    void updateModelsList();
    PCDMModel * selectedModel();
    void selectModel(const QDateTime & timestamp);
    void updateModelSummary();
    void renameSelectedModel();
    void resetToSelectedModel();
    void deleteSelectedModel();

private:
    const QString m_settingsGroup;
    GuiPluginInterface & m_pluginInterface;
    QStringList m_recentProjects;

    std::unique_ptr<Ui_PCDMWidget> m_ui;
    std::unique_ptr<QMenu> m_projectMenu;
    QAction * m_closeProjectAction;
    QAction * m_showProjectFolderAction;
    QMenu * m_recentProjectsMenu;
    std::unique_ptr<QStateMachine> m_stateMachine;
    std::unique_ptr<PCDMWidget_StateHelper> m_stateHelper;
    std::unique_ptr<DataSetFilter> m_coordsDataSetFilter;
    std::unique_ptr<PCDMVisualizationGenerator> m_visGenerator;
    bool m_firstShowEventHandlingRequired;

    std::unique_ptr<PCDMProject> m_project;
};
