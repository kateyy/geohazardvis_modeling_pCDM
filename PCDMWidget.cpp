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

#include "PCDMWidget.h"
#include "ui_PCDMWidget.h"

#include <cassert>
#include <limits>

#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStateMachine>

#include <vtkDataSet.h>
#include <vtkSmartPointer.h>

#include <core/CoordinateSystems.h>
#include <core/data_objects/CoordinateTransformableDataObject.h>
#include <core/utility/conversions.h>
#include <core/utility/DataExtent.h>
#include <core/utility/DataSetFilter.h>
#include <core/utility/qthelper.h>
#include <gui/plugin/GuiPluginInterface.h>

#include "PCDMCreateProjectDialog.h"
#include "PCDMModel.h"
#include "PCDMProject.h"
#include "PCDMVisualizationGenerator.h"
#include "PCDMWidget_StateHelper.h"


namespace
{

const auto degreeSign = QChar(0xb0);

}

using pCDM::t_FP;


PCDMWidget::PCDMWidget(
    const QString & settingsGroup,
    GuiPluginInterface & pluginInterface,
    QWidget * parent,
    Qt::WindowFlags flags)
    : QDockWidget(parent, flags)
    , m_settingsGroup{ settingsGroup }
    , m_pluginInterface{ pluginInterface }
    , m_ui{ std::make_unique<Ui_PCDMWidget>() }
    , m_stateMachine{ std::make_unique<QStateMachine>() }
    , m_stateHelper{ std::make_unique<PCDMWidget_StateHelper>() }
    , m_visGenerator{ std::make_unique<PCDMVisualizationGenerator>(pluginInterface.dataMapping()) }
    , m_firstShowEventHandlingRequired{ true }
{
    m_ui->setupUi(this);
    m_ui->savedModelsTable->sortByColumn(0, Qt::SortOrder::DescendingOrder);

    m_projectMenu = std::make_unique<QMenu>();
    auto projectNewAction = m_projectMenu->addAction("&New");
    auto projectOpenAction = m_projectMenu->addAction("&Open");
    m_closeProjectAction = m_projectMenu->addAction("&Close");
    m_showProjectFolderAction = m_projectMenu->addAction("Show Project &Folder");
    m_recentProjectsMenu = m_projectMenu->addMenu("&Recent Projects");

    m_ui->projectMenuButton->setMenu(m_projectMenu.get());

    projectNewAction->setIcon(qApp->style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    projectOpenAction->setIcon(qApp->style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_closeProjectAction->setIcon(qApp->style()->standardIcon(QStyle::SP_DialogCloseButton));

    connect(projectNewAction, &QAction::triggered, this, &PCDMWidget::newProjectDialog);
    connect(projectOpenAction, &QAction::triggered, this, &PCDMWidget::openProjectDialog);
    connect(m_closeProjectAction, &QAction::triggered, [this] ()
    {
        loadProjectFrom({});
    });
    connect(m_showProjectFolderAction, &QAction::triggered, [this] ()
    {
        if (m_project)
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_project->rootFolder()));
        }
    });
    connect(m_recentProjectsMenu, &QMenu::triggered, [this] (QAction * action)
    {
        if (action)
        {
            checkLoadProjectFrom(action->data().toString(), true);
        }
    });

    connect(m_ui->surfaceSaveButton, &QAbstractButton::clicked, this, &PCDMWidget::saveSurfaceParameters);

    connect(m_ui->runButton, &QAbstractButton::clicked, this, &PCDMWidget::runModel);
    connect(m_ui->saveModelButton, &QAbstractButton::clicked, this, &PCDMWidget::saveModelDialog);
    connect(m_ui->openVisualizationButton, &QAbstractButton::clicked, this, &PCDMWidget::showVisualization);
    connect(m_ui->visualizeResidualsButton, &QAbstractButton::clicked, this, &PCDMWidget::showResidual);

    connect(m_ui->savedModelsTable, &QTableWidget::itemSelectionChanged, this, &PCDMWidget::updateModelSummary);
    connect(m_ui->renameModelButton, &QAbstractButton::clicked, this, &PCDMWidget::renameSelectedModel);
    connect(m_ui->deleteModelButton, &QAbstractButton::clicked, this, &PCDMWidget::deleteSelectedModel);
    connect(m_ui->resetToSelectedButton, &QAbstractButton::clicked, this, &PCDMWidget::resetToSelectedModel);
    connect(m_ui->savedModelsTable, &QTableWidget::doubleClicked, this, &PCDMWidget::resetToSelectedModel);

    setupStateMachine();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
}

PCDMWidget::~PCDMWidget()
{
    saveSettings();
}

void PCDMWidget::showEvent(QShowEvent * /*event*/)
{
    if (!m_firstShowEventHandlingRequired)
    {
        return;
    }

    m_firstShowEventHandlingRequired = false;

    loadSettings();
}

void PCDMWidget::setupStateMachine()
{
    assert(m_stateMachine && m_stateHelper);
    auto & machine = *m_stateMachine;
    auto * helper = m_stateHelper.get();

    // No project is loaded, nothing can be done except loading or creating a project.
    auto sNoProject = new QState();
    // A project is loaded and setup or modeling steps can be done.
    auto sProjectLoaded = new QState();

    // The user inputs new surface coordinates and sets nu.
    auto sSetupSurface = new QState(sProjectLoaded);
    // The user setups a new surface while resetting to a previous valid one is possible.
    auto sSetupHasValidSurface = new QState(sSetupSurface);
    // The user setups a new surface and a previous valid surface is not available.
    auto sInvalidSurface = new QState(sSetupSurface);

    // Setup steps are done and the user inputs pCDM parameters.
    auto sModelUserInput = new QState(sProjectLoaded);
    // The model is currently being computed. Most UI part are blocked.
    auto sComputeModel = new QState(sProjectLoaded);

    sProjectLoaded->addTransition(helper, &PCDMWidget_StateHelper::projectUnloaded, sNoProject);

    sNoProject->addTransition(helper, &PCDMWidget_StateHelper::projectLoadedWithValidSurface, sModelUserInput);
    sNoProject->addTransition(helper, &PCDMWidget_StateHelper::projectLoadedWithInvalidSurface, sInvalidSurface);

    sSetupHasValidSurface->addTransition(m_ui->surfaceCancelButton, &QAbstractButton::clicked, sModelUserInput);
    sSetupSurface->addTransition(helper, &PCDMWidget_StateHelper::validSurfaceSaved, sModelUserInput);
    sModelUserInput->addTransition(m_ui->surfaceSetupButton, &QAbstractButton::clicked, sSetupHasValidSurface);

    sModelUserInput->addTransition(helper, &PCDMWidget_StateHelper::computingModel, sComputeModel);
    sComputeModel->addTransition(helper, &PCDMWidget_StateHelper::computingEnded, sModelUserInput);


    machine.setGlobalRestorePolicy(QState::RestoreProperties);
    m_ui->progressBar->hide();
    m_ui->surfaceStackedWidget->setCurrentIndex(0);
    m_ui->modelingTabWidget->setCurrentIndex(0);

    sNoProject->assignProperty(m_closeProjectAction, "enabled", false);
    sNoProject->assignProperty(m_showProjectFolderAction, "enabled", false);
    sNoProject->assignProperty(m_ui->surfaceGroupBox, "enabled", false);
    sNoProject->assignProperty(m_ui->modelingTabWidget, "enabled", false);

    sSetupSurface->assignProperty(m_ui->surfaceStackedWidget, "currentIndex", 1);
    sInvalidSurface->assignProperty(m_ui->surfaceCancelButton, "visible", false);

    sModelUserInput->assignProperty(m_ui->modelingTabWidget, "currentIndex", 0);

    sComputeModel->assignProperty(m_ui->progressBar, "visible", true);
    sComputeModel->assignProperty(m_ui->projectWidget, "enabled", false);
    sComputeModel->assignProperty(m_ui->surfaceGroupBox, "enabled", false);
    sComputeModel->assignProperty(m_ui->pCDMPositionGroup, "enabled", false);
    sComputeModel->assignProperty(m_ui->pCDMRotationGroup, "enabled", false);
    sComputeModel->assignProperty(m_ui->pCDMPotenciesGroup, "enabled", false);
    sComputeModel->assignProperty(m_ui->runButton, "enabled", false);
    sComputeModel->assignProperty(m_ui->saveModelButton, "enabled", false);
    sComputeModel->assignProperty(m_ui->openVisualizationButton, "enabled", false);
    sComputeModel->assignProperty(m_ui->visualizeResidualsButton, "enabled", false);
    sComputeModel->assignProperty(m_ui->savedModelsTab, "enabled", false);


    connect(sSetupSurface, &QState::entered, this, &PCDMWidget::prepareSetupSurfaceParameters);
    connect(sSetupSurface, &QState::exited, this, &PCDMWidget::cleanupSurfaceParameterSetup);

    machine.addState(sNoProject);
    machine.addState(sProjectLoaded);
    machine.setInitialState(sNoProject);

    machine.start();
}

void PCDMWidget::loadSettings()
{
    m_pluginInterface.readSettings(m_settingsGroup, [this] (const QSettings & settings)
    {
        m_recentProjects = settings.value("RecentProjects").toStringList();

        const auto projectToLoad = settings.value("LastProject").toString();
        if (!projectToLoad.isEmpty() && PCDMProject::checkFolderIsProject(projectToLoad))
        {
            checkLoadProjectFrom(projectToLoad, false);
        }
    });

    updateRecentProjectsMenu();
}

void PCDMWidget::saveSettings()
{
    m_pluginInterface.readWriteSettings(m_settingsGroup, [this] (QSettings & settings)
    {
        settings.setValue("RecentProjects", m_recentProjects);
        settings.setValue("LastProject", m_project ? m_project->rootFolder() : "");
    });
}

void PCDMWidget::prependRecentProject(const QString & projectRootFolder)
{
    const int previousIndex = m_recentProjects.indexOf(projectRootFolder);
    if (previousIndex == 0)
    {
        // everything is fine already
        return;
    }
    if (previousIndex > 0)
    {
        m_recentProjects.removeAt(previousIndex);
    }

    m_recentProjects.prepend(projectRootFolder);

    updateRecentProjectsMenu();
}

void PCDMWidget::removeRecentProject(const QString & projectRootFolder)
{
    if (m_recentProjects.removeOne(projectRootFolder))
    {
        updateRecentProjectsMenu();
    }
}

void PCDMWidget::updateRecentProjectsMenu()
{
    m_recentProjectsMenu->clear();

    for (int i = 0; i < m_recentProjects.size(); ++i)
    {
        const auto numberString = i < 10
            ? "&" + QString::number(i)
            : (i == 10
                ? "1&0"
                : QString::number(i));
        const auto & projectDir = m_recentProjects[i];
        m_recentProjectsMenu->addAction(numberString + " " + projectDir)->setData(projectDir);
    }
}

void PCDMWidget::openProjectDialog()
{
    QDir searchDir(m_project ? m_project->rootFolder() : "");
    searchDir.cdUp();
    const auto newPath = QFileDialog::getOpenFileName(this,
        "Select project root folder", searchDir.absolutePath(), PCDMProject::projectFileNameFilter());

    if (newPath.isEmpty())
    {
        return;
    }
    const auto rootFolder = QFileInfo(newPath).absoluteDir().path();

    QString errorMessage;
    if (!PCDMProject::checkFolderIsProject(rootFolder, &errorMessage))
    {
        QMessageBox::warning(this, "Project Selection",
            "Cannot open the selected project. " + errorMessage);
        return;
    }

    loadProjectFrom(rootFolder);
}

void PCDMWidget::newProjectDialog()
{
    QDir currentBaseDir(m_project ? m_project->rootFolder() : "");
    currentBaseDir.cdUp();
    const QString newProjectPath =
        PCDMCreateProjectDialog::getNewProjectPath(this, currentBaseDir.path());
    if (newProjectPath.isEmpty())
    {
        return;
    }
    loadProjectFrom(newProjectPath);
}

void PCDMWidget::loadProjectFrom(const QString & rootFolder)
{
    // Cleanup previously loaded project
    if (m_project)
    {
        if (m_project->rootFolder() == rootFolder)
        {
            return;
        }

        const auto toBeDeleted = std::move(m_project);

        m_visGenerator->setProject(nullptr);

        updateSurfaceSummary();
        updateModelsList();
        m_ui->projectNameEdit->setText("");

        emit m_stateHelper->projectUnloaded();

        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    // Save cleaned-up state in case loading the project causes application failure.
    saveSettings();

    if (rootFolder.isEmpty())
    {
        return;
    }

    m_project = std::make_unique<PCDMProject>(rootFolder);
    m_ui->projectNameEdit->setText(QDir(rootFolder).dirName());

    m_visGenerator->setProject(m_project.get());
    m_visGenerator->dataObject();   // Initialize the preview data and pass it to the UI

    updateSurfaceSummary();

    if (m_project->horizontalCoordinatesDataSet())
    {
        emit m_stateHelper->projectLoadedWithValidSurface();
    }
    else
    {
        emit m_stateHelper->projectLoadedWithInvalidSurface();
    }

    updateModelsList();

    if (m_project->model(m_project->lastModelTimestamp()))
    {
        selectModel(m_project->lastModelTimestamp());
        resetToSelectedModel();
    }
    else
    {
        // no previous model -> clear UI source parameters
        sourceParametersToUi({});
    }

    prependRecentProject(rootFolder);
    saveSettings();
}

void PCDMWidget::checkLoadProjectFrom(const QString & rootFolder, bool reportError)
{
    QString errorMessage;
    if (!PCDMProject::checkFolderIsProject(rootFolder, &errorMessage))
    {
        if (reportError)
        {
            QMessageBox::warning(this, "Project Selection",
                "Cannot open the selected project. " + errorMessage);
        }
        removeRecentProject(rootFolder);
        return;
    }
    loadProjectFrom(rootFolder);
}

void PCDMWidget::prepareSetupSurfaceParameters()
{
    m_coordsDataSetFilter = std::make_unique<DataSetFilter>(m_pluginInterface.dataSetHandler());

    connect(m_coordsDataSetFilter.get(), &DataSetFilter::listChanged,
        [this] (const QList<DataObject *> & filteredList)
    {
        const QSignalBlocker signalBlocker(m_ui->coordsDataSetComboBox);

        const auto previousSelection = variantToDataObjectPtr(m_ui->coordsDataSetComboBox->currentData());
        int restoredSelectionIndex = -1;
        m_ui->coordsDataSetComboBox->clear();
        m_ui->coordsDataSetComboBox->addItem("");

        for (int i = 0; i < filteredList.size(); ++i)
        {
            const auto dataObject = filteredList[i];
            m_ui->coordsDataSetComboBox->addItem(
                dataObject->name(),
                dataObjectPtrToVariant(dataObject));

            if (dataObject == previousSelection)
            {
                restoredSelectionIndex = i;
            }
        }

        m_ui->coordsDataSetComboBox->setCurrentIndex(restoredSelectionIndex);
    });

    m_coordsDataSetFilter->setFilterFunction(
        [] (DataObject * dataObject, const DataSetHandler &) -> bool
    {
        auto transformable = dynamic_cast<CoordinateTransformableDataObject *>(dataObject);
        if (!transformable)
        {
            return false;
        }

        auto coordsSystem = transformable->coordinateSystem();
        coordsSystem.type = CoordinateSystemType::metricLocal;
        if (coordsSystem.unitOfMeasurement.isEmpty())
        {
            coordsSystem.unitOfMeasurement = "km";
        }

        return transformable->canTransformTo(coordsSystem);
    });

    if (m_project)
    {
        m_ui->poissonsRatioEdit->setValue(m_project->poissonsRatio());
    }
}

void PCDMWidget::cleanupSurfaceParameterSetup()
{
    m_coordsDataSetFilter = {};
    m_ui->coordsDataSetComboBox->clear();
}

void PCDMWidget::saveSurfaceParameters()
{
    assert(m_project);

    static const QString title = "Surface Coordinates Setup";

    bool keepCurrentGeometry = false;
    auto * const selectedDataObject = variantToDataObjectPtr(m_ui->coordsDataSetComboBox->currentData());
    if (!selectedDataObject && m_project->horizontalCoordinatesDataSet())
    {
        keepCurrentGeometry = true;
    }

    if (!keepCurrentGeometry && (m_ui->coordsDataSetComboBox->count() == 1))
    {
        QMessageBox::information(this, title,
            "Please load or import data sets with point coordinates to continue with the modeling setup.");
        return;
    }

    if (!keepCurrentGeometry && !selectedDataObject)
    {
        QMessageBox::information(this, title,
            "Please select a data set that defines horizontal coordinates for the modeling setup.");
        return;
    }

    if (!keepCurrentGeometry)
    {
        assert(dynamic_cast<CoordinateTransformableDataObject *>(selectedDataObject));
        auto transformable = static_cast<CoordinateTransformableDataObject *>(selectedDataObject);
        auto coordinateSystem = transformable->coordinateSystem();
        coordinateSystem.type = CoordinateSystemType::metricLocal;
        if (coordinateSystem.unitOfMeasurement.isEmpty())
        {
            coordinateSystem.unitOfMeasurement = "km";
        }
        auto localDataSet = transformable->coordinateTransformedDataSet(coordinateSystem);

        if (!localDataSet)
        {
            QMessageBox::warning(this, title,
                "The selected data set (" + selectedDataObject->name() + ") cannot be transformed to "
                "local metric coordinates. Please select a compatible data set.");
            return;
        }

        if (localDataSet->GetNumberOfPoints() == 0)
        {
            QMessageBox::warning(this, title,
                "The selected data set (" + selectedDataObject->name() + ") does not contain point coordinates.");
            return;
        }

        if (!m_project->importHorizontalCoordinatesFrom(*localDataSet))
        {
            QMessageBox::warning(this, title,
                "Could not import coordinates from the selected data set (" + selectedDataObject->name() + ")."
                "Maybe the data set type is not supported, or the data set is empty.");
            return;
        }
    }
    assert(m_project->horizontalCoordinatesDataSet());

    m_project->setPoissonsRatio(static_cast<pCDM::t_FP>(m_ui->poissonsRatioEdit->value()));

    updateSurfaceSummary();

    emit m_stateHelper->validSurfaceSaved();
}

void PCDMWidget::updateSurfaceSummary()
{
    if (!m_project)
    {
        m_ui->surfaceSummaryTable->setRowCount(0);
        return;
    }

    vtkIdType numCoordinates = 0;
    DataBounds bounds;
    QString dataType;
    QString coordsUnitSuffix;
    if (auto dataSet = m_project->horizontalCoordinatesDataSet())
    {
        numCoordinates = dataSet->GetNumberOfPoints();
        dataSet->GetBounds(bounds.data());
        const auto spec = CoordinateSystemSpecification::fromFieldData(*dataSet->GetFieldData());
        coordsUnitSuffix = " " + spec.unitOfMeasurement;
    }

    {
        QTableWidgetSetRowsWorker addRow{ *m_ui->surfaceSummaryTable };

        auto && coords = m_project->coordinateSystem();

        auto && unit = coords.unitOfMeasurement;

        addRow("Poisson's Ratio", QString::number(m_project->poissonsRatio()));
        addRow("Geometry", m_project->horizontalCoordinatesGeometryType());
        addRow("Number of Coordinates", QString::number(numCoordinates));
        addRow("Extent (West-East)", bounds.isEmpty() ? ""
            : QString::number(bounds.dimension(0)[0]) + unit + ", "
            + QString::number(bounds.dimension(0)[1]) + unit);
        addRow("Extent (South-North)", bounds.isEmpty() ? ""
            : QString::number(bounds.dimension(1)[0]) + unit + ", "
            + QString::number(bounds.dimension(1)[1]) + unit);
        QString referencePointStr;
        if (coords.isReferencePointValid())
        {
            auto && latLong = coords.referencePointLatLong;
            referencePointStr =
                QString::number(latLong[0]) + degreeSign + (latLong[0] >= 0 ? "N" : "S") + " "
                + QString::number(latLong[1]) + degreeSign + (latLong[1] >= 0 ? "E" : "W")
                + " (" + coords.geographicSystem + ")";
        }
        else
        {
            referencePointStr = "(unspecified)";
        }
        addRow("Reference Point", referencePointStr);
    }

    m_ui->surfaceSummaryTable->resizeColumnToContents(0);
    m_ui->surfaceSummaryTable->resizeRowsToContents();

    m_ui->positionXSpinBox->setSuffix(coordsUnitSuffix);
    m_ui->positionYSpinBox->setSuffix(coordsUnitSuffix);
    m_ui->depthSpinBox->setSuffix(coordsUnitSuffix);
}

void PCDMWidget::runModel()
{
    if (!m_project)
    {
        return;
    }

    static const QString title = "pCDM Modeling";

    const auto sourceParams = sourceParametersFromUi();
    QString errorMessage;
    if (!sourceParams.isValid(&errorMessage))
    {
        QMessageBox::warning(this, title,
            "The supplied point CDM parameters are not valid: " + errorMessage);
        return;
    }

    PCDMModel * modelPtr = nullptr;
    auto lastModel = m_project->model(m_project->lastModelTimestamp());
    if (lastModel && (lastModel->parameters() == sourceParams))
    {
        modelPtr = lastModel;
    }
    else
    {
        modelPtr = m_project->addModel();
    }

    if (!modelPtr)
    {
        QMessageBox::warning(this, title,
            "Could not create required files in the project folder. Please make sure that the "
            "project folder is accessible for writing.");
        return;
    }
    auto & model = *modelPtr;
    model.setParameters(sourceParams);

    m_project->setLastModelTimestamp(model.timestamp());

    connect(&model, &PCDMModel::requestCompleted, this, &PCDMWidget::handleModelDone);
    emit m_stateHelper->computingModel();
    model.requestResultsAsync();
}

void PCDMWidget::handleModelDone()
{
    assert(m_project);
    auto modelPtr = m_project->model(m_project->lastModelTimestamp());
    assert(modelPtr);
    if (!modelPtr)
    {
        return;
    }

    disconnect(modelPtr, &PCDMModel::requestCompleted, this, &PCDMWidget::handleModelDone);

    auto & model = *modelPtr;

    emit m_stateHelper->computingEnded();

    if (model.errorFlags() != PCDMModel::noError
        || !model.hasResults() || model.results()[0].empty())
    {
        if (model.errorFlags().testFlag(PCDMModel::outOfMemory))
        {
            QMessageBox::warning(this, "pCDM Modeling",
                "Not enough main memory to compute the current model. Please try to close other "
                "applications and rerun the model, or choose a smaller model setup.");
            return;
        }
        QMessageBox::critical(this, "pCDM Modeling",
            "An unexpected error occurred in the modeling back-end.");
        return;
    }

    updateModelsList();
    m_visGenerator->setModel(model);
}

void PCDMWidget::saveModelDialog()
{
    if (!m_project)
    {
        return;
    }

    const auto uiParams = sourceParametersFromUi();

    QString modelName;

    // Check if the previously run/saved model is still represented in the UI.
    // Otherwise, add a new model to the project.
    const auto previousModel = m_project->model(m_project->lastModelTimestamp());
    if (previousModel && (previousModel->parameters() == uiParams))
    {
        modelName = previousModel->name();
    }

    bool okay;
    modelName = QInputDialog::getText(this,
        "Model Name", "Set a name for the current model",
        QLineEdit::Normal, modelName, &okay);
    if (!okay)
    {
        return;
    }

    auto model = previousModel ? previousModel : m_project->addModel();
    model->setName(modelName);

    updateModelsList();
}

void PCDMWidget::showVisualization()
{
    if (!m_project)
    {
        return;
    }

    auto model = m_project->model(m_project->lastModelTimestamp());
    if (!model)
    {
        m_visGenerator->showDataObject();
        return;
    }

    m_visGenerator->showModel(*model);
}

void PCDMWidget::showResidual()
{
    if (!m_project)
    {
        return;
    }

    auto model = m_project->model(m_project->lastModelTimestamp());
    if (!model)
    {
        m_visGenerator->showDataObjectsInResidualView();
        return;
    }

    m_visGenerator->showResidualForModel(*model);
}

void PCDMWidget::sourceParametersToUi(const pCDM::PointCDMParameters & parameters)
{
    m_ui->positionXSpinBox->setValue(parameters.horizontalCoord[0]);
    m_ui->positionYSpinBox->setValue(parameters.horizontalCoord[1]);
    m_ui->depthSpinBox->setValue(parameters.depth);
    m_ui->omegaXSpinBox->setValue(parameters.omega[0]);
    m_ui->omegaYSpinBox->setValue(parameters.omega[1]);
    m_ui->omegaZSpinBox->setValue(parameters.omega[2]);
    m_ui->dvXSpinBox->setValue(parameters.dv[0]);
    m_ui->dvYSpinBox->setValue(parameters.dv[1]);
    m_ui->dvZSpinBox->setValue(parameters.dv[2]);
}

pCDM::PointCDMParameters PCDMWidget::sourceParametersFromUi() const
{
    pCDM::PointCDMParameters p;
    p.horizontalCoord[0] = static_cast<t_FP>(m_ui->positionXSpinBox->value());
    p.horizontalCoord[1] = static_cast<t_FP>(m_ui->positionYSpinBox->value());
    p.depth = static_cast<t_FP>(m_ui->depthSpinBox->value());
    p.omega[0] = static_cast<t_FP>(m_ui->omegaXSpinBox->value());
    p.omega[1] = static_cast<t_FP>(m_ui->omegaYSpinBox->value());
    p.omega[2] = static_cast<t_FP>(m_ui->omegaZSpinBox->value());
    p.dv[0] = static_cast<t_FP>(m_ui->dvXSpinBox->value());
    p.dv[1] = static_cast<t_FP>(m_ui->dvYSpinBox->value());
    p.dv[2] = static_cast<t_FP>(m_ui->dvZSpinBox->value());
    return p;
}

void PCDMWidget::updateModelsList()
{
    m_ui->savedModelsTable->setSortingEnabled(false);
    QDateTime lastSelection;
    int lastSelectionIndex = -1;
    {
        const auto rows = m_ui->savedModelsTable->selectionModel()->selectedRows();
        if (!rows.isEmpty())
        {
            lastSelectionIndex = rows.first().row();
            lastSelection = m_ui->savedModelsTable->item(lastSelectionIndex, 0)->data(Qt::DisplayRole).toDateTime();
        }
    }

    const QSignalBlocker signalBlocker(m_ui->savedModelsTable);

    m_ui->savedModelsTable->clearContents();
    m_ui->savedModelsTable->setRowCount(0);

    if (!m_project || m_project->models().empty())
    {
        updateModelSummary();
        return;
    }

    auto && models = m_project->models();
    const int numModels = models.size() > static_cast<size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max() : static_cast<int>(models.size());

    m_ui->savedModelsTable->setRowCount(numModels);

    auto it = models.begin();
    int restoredSelectionIndex = -1;
    for (int i = 0; i < numModels; ++i, ++it)
    {
        auto timestampItem = new QTableWidgetItem();
        timestampItem->setData(Qt::DisplayRole, it->first);
        m_ui->savedModelsTable->setItem(i, 0, timestampItem);
        m_ui->savedModelsTable->setItem(i, 1, new QTableWidgetItem(it->second->name()));

        if (lastSelection == it->first)
        {
            restoredSelectionIndex = i;
        }
    }

    if (restoredSelectionIndex == -1)
    {
        restoredSelectionIndex = std::min(lastSelectionIndex, numModels - 1);
    }

    m_ui->savedModelsTable->selectRow(restoredSelectionIndex);
    if (restoredSelectionIndex != -1)
    {
        m_ui->savedModelsTable->scrollTo(
            m_ui->savedModelsTable->rootIndex().child(restoredSelectionIndex, 0));
    }

    m_ui->savedModelsTable->setSortingEnabled(true);

    m_ui->savedModelsTable->resizeColumnToContents(0);
    m_ui->savedModelsTable->resizeRowsToContents();

    updateModelSummary();
}

PCDMModel * PCDMWidget::selectedModel()
{
    const auto selection = m_ui->savedModelsTable->selectionModel()->selectedRows();
    if (selection.isEmpty())
    {
        return{};
    }
    assert(m_project);

    const int row = selection.first().row();
    const auto timestamp = m_ui->savedModelsTable->item(row, 0)->data(Qt::DisplayRole).toDateTime();
    return m_project->model(timestamp);
}

void PCDMWidget::selectModel(const QDateTime & timestamp)
{
    if (!m_project)
    {
        return;
    }

    for (int r = 0; r < m_ui->savedModelsTable->rowCount(); ++r)
    {
        if (timestamp ==
            m_ui->savedModelsTable->item(r, 0)->data(Qt::DisplayRole).toDateTime())
        {
            m_ui->savedModelsTable->selectRow(r);
            return;
        }
    }

}

void PCDMWidget::updateModelSummary()
{
    if (!m_project)
    {
        return;
    }

    const auto selection = m_ui->savedModelsTable->selectionModel()->selectedRows();
    int row = selection.isEmpty() ? -1 : selection.first().row();
    if (row < 0)
    {
        m_ui->selectedModelSummary->clear();
        return;
    }

    const auto timestamp = m_ui->savedModelsTable->item(row, 0)->data(Qt::DisplayRole).toDateTime();
    auto modelPtr = m_project->model(timestamp);
    assert(modelPtr);
    auto & model = *modelPtr;
    auto && params = model.parameters();

    auto && coordsSpec = m_project->coordinateSystem();
    auto && metricUnit = coordsSpec.unitOfMeasurement;
    auto && xy = params.horizontalCoord;

    QString previewText;
    QTextStream stream(&previewText);
    stream
        << "Creation date: " << timestamp.toString() << endl
        << "Name: " << model.name() << endl
        << "Results stored: " << (model.hasResults() ? "yes" : "no") << endl
        << endl
        << "Horizontal position: "
            << std::abs(xy[1]) << metricUnit << " " << (xy[1] >= 0 ? "North" : "South") << ", "
            << std::abs(xy[0]) << metricUnit << " " << (xy[0] >= 0 ? "East" : "West") << endl
        << "Depth: " << params.depth << metricUnit << endl
        << "Rotation: " << arrayToString(params.omega, ", ", {}, degreeSign) << endl
        << "Potencies: " << arrayToString(params.dv) << endl;

    m_ui->selectedModelSummary->setText(previewText);
}

void PCDMWidget::renameSelectedModel()
{
    const auto model = selectedModel();

    if (!model)
    {
        return;
    }

    bool okay;
    const auto modelName = QInputDialog::getText(this,
        "Model Name", "Set a name for the current model",
        QLineEdit::Normal, model->name(), &okay);
    if (!okay)
    {
        return;
    }

    model->setName(modelName);

    updateModelsList();
}

void PCDMWidget::resetToSelectedModel()
{
    const auto model = selectedModel();
    if (!model)
    {
        return;
    }

    sourceParametersToUi(model->parameters());
    m_project->setLastModelTimestamp(model->timestamp());
    m_visGenerator->setModel(*model);

    m_ui->modelingTabWidget->setCurrentIndex(0);
}

void PCDMWidget::deleteSelectedModel()
{
    const auto selection = m_ui->savedModelsTable->selectionModel()->selectedRows();
    if (selection.isEmpty())
    {
        return;
    }
    assert(m_project);

    const int count = selection.size();

    if (QMessageBox::question(this, "pCDM Project",
        count == 1
        ? "Do you want to delete the selected model?"
        : "Do you want to delete " + QString::number(count) + " selected models?")
        != QMessageBox::Yes)
    {
        return;
    }

    const QSignalBlocker signalBlocker(m_ui->savedModelsTable);

    for (auto && item : selection)
    {
        const int row = item.row();
        const auto timestamp = m_ui->savedModelsTable->item(row, 0)->data(Qt::DisplayRole).toDateTime();
        m_project->deleteModel(timestamp);
    }

    updateModelsList();
}
