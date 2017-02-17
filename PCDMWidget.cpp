#include "PCDMWidget.h"
#include "ui_PCDMWidget.h"

#include <cassert>
#include <limits>

#include <QDesktopServices>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStateMachine>

#include <vtkDataSet.h>
#include <vtkSmartPointer.h>

#include <core/CoordinateSystems.h>
#include <core/data_objects/CoordinateTransformableDataObject.h>
#include <core/utility/DataExtent.h>
#include <core/utility/DataSetFilter.h>
#include <core/utility/qthelper.h>
#include <gui/plugin/GuiPluginInterface.h>

#include "PCDMModel.h"
#include "PCDMProject.h"
#include "PCDMVisualizationGenerator.h"
#include "PCDMWidget_StateHelper.h"


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
{
    m_ui->setupUi(this);

    m_projectMenu = std::make_unique<QMenu>();
    auto projectNewAction = m_projectMenu->addAction("&New");
    auto projectOpenAction = m_projectMenu->addAction("&Open");
    m_closeProjectAction = m_projectMenu->addAction("&Close");
    m_showProjectFolderAction = m_projectMenu->addAction("Show Project &Folder");
    m_recentProjectsMenu = m_projectMenu->addMenu("&Recent Projects");

    connect(m_ui->projectMenuButton, &QAbstractButton::clicked, [this] ()
    {
        m_projectMenu->exec(m_ui->projectMenuButton->mapToGlobal(
            QPoint(0, m_ui->projectMenuButton->height())));
    });

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
    connect(m_ui->openVisualizationButton, &QAbstractButton::clicked, this, &PCDMWidget::showVisualization);

    setupStateMachine();

    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    loadSettings();
}

PCDMWidget::~PCDMWidget()
{
    saveSettings();
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
    sComputeModel->assignProperty(m_ui->openVisualizationButton, "enabled", false);
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

    QString errorMessage;
    if (!PCDMProject::checkFolderIsProject(newPath, &errorMessage))
    {
        QMessageBox::warning(this, "Project Selection",
            "Cannot open the selected project. " + errorMessage);
        return;
    }

    loadProjectFrom(QFileInfo(newPath).absoluteDir().path());
}

void PCDMWidget::newProjectDialog()
{
    QDir currentBaseDir(m_project ? m_project->rootFolder() : "");
    currentBaseDir.cdUp();
    QString newProjectPath;

    do
    {
        newProjectPath = QFileDialog::getExistingDirectory(this, "Select new project root folder",
            currentBaseDir.absolutePath());

        if (newProjectPath.isEmpty())
        {
            return;
        }

        if (PCDMProject::checkFolderIsProject(newProjectPath))
        {
            const auto answer = QMessageBox::question(this,
                "Project Selection",
                "The selected folder already contains a pCDM project.\nDo you want to load this project?",
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (answer == QMessageBox::Yes)
            {
                break;
            }
            if (answer == QMessageBox::No)
            {
                continue;   // let the user try again
            }
            return;         // Cancel
        }

        if (QDir(newProjectPath).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries).count() > 0)
        {
            const auto answer = QMessageBox::information(this,
                "Project Selection",
                "The selected folder is not empty.\nPlease select an empty folder for the new project!",
                QMessageBox::Ok | QMessageBox::Cancel);
            if (answer == QMessageBox::Ok)
            {
                continue;
            }
            return;         // Cancel
        }

        break;

    } while (true);

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

        // TODO store GUI session

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

    if (!m_project->models().empty())
    {
        m_lastModelTimestamp = m_project->models().rbegin()->first;
    }

    m_visGenerator->setProject(m_project.get());

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

    // project/model preview data set to ui
    // (force) Update preview

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

    if (m_ui->coordsDataSetComboBox->count() == 0)
    {
        QMessageBox::information(this, title,
            "Please load or import data sets with point coordinates to continue with the modeling setup.");
        return;
    }

    auto * const selectedDataObject = variantToDataObjectPtr(m_ui->coordsDataSetComboBox->currentData());

    if (!selectedDataObject)
    {
        QMessageBox::information(this, title,
            "Please select a data set that defined horizontal coordinates for the modeling setup.");
        return;
    }

    assert(dynamic_cast<CoordinateTransformableDataObject *>(selectedDataObject));
    auto transformable = static_cast<CoordinateTransformableDataObject *>(selectedDataObject);
    auto coordinateSystem = transformable->coordinateSystem();
    coordinateSystem.type = CoordinateSystemType::metricLocal;
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
    if (auto dataSet = m_project->horizontalCoordinatesDataSet())
    {
        numCoordinates = dataSet->GetNumberOfPoints();
        dataSet->GetBounds(bounds.data());
    }

    int row = 0;
    m_ui->surfaceSummaryTable->setColumnCount(2);
    m_ui->surfaceSummaryTable->setRowCount(5);
    m_ui->surfaceSummaryTable->setItem(row, 0, new QTableWidgetItem("Poisson's Ratio"));
    m_ui->surfaceSummaryTable->setItem(row++, 1,
        new QTableWidgetItem(QString::number(m_project->poissonsRatio())));
    m_ui->surfaceSummaryTable->setItem(row, 0, new QTableWidgetItem("Geometry"));
    m_ui->surfaceSummaryTable->setItem(row++, 1,
        new QTableWidgetItem(m_project->horizontalCoordinatesGeometryType()));
    m_ui->surfaceSummaryTable->setItem(row, 0, new QTableWidgetItem("Number of Coordinates"));
    m_ui->surfaceSummaryTable->setItem(row++, 1,
        new QTableWidgetItem(QString::number(numCoordinates)));
    m_ui->surfaceSummaryTable->setItem(row, 0, new QTableWidgetItem("Extent (West-East)"));
    m_ui->surfaceSummaryTable->setItem(row++, 1, new QTableWidgetItem(
        bounds.isEmpty() ? ""
        : QString::number(bounds.dimension(0)[0]) + "; " + QString::number(bounds.dimension(0)[1])));
    m_ui->surfaceSummaryTable->setItem(row++, 0, new QTableWidgetItem("Extent (South-North)"));
    m_ui->surfaceSummaryTable->setItem(row++, 1, new QTableWidgetItem(
        bounds.isEmpty() ? ""
        : QString::number(bounds.dimension(1)[0]) + "; " + QString::number(bounds.dimension(1)[1])));

    m_ui->surfaceSummaryTable->resizeColumnsToContents();
    m_ui->surfaceSummaryTable->resizeRowsToContents();
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

    auto modelPtr = m_project->addModel();
    if (!modelPtr)
    {
        QMessageBox::warning(this, title,
            "Could not create required files in the project folder. Please make sure that the "
            "project folder is accessible for writing.");
        return;
    }
    auto & model = *modelPtr;
    m_lastModelTimestamp = model.timestamp();

    model.setParameters(sourceParams);

    model.requestResultsAsync();
    emit m_stateHelper->computingModel();

    // TODO responsive GUI for very large setups
    qApp->processEvents();

    const bool result = model.waitForResults();
    emit m_stateHelper->computingEnded();

    if (!result)
    {
        QMessageBox::critical(this, "pCDM Modeling", "An error occurred in the modeling back-end.");
        return;
    }

    m_visGenerator->showModel(model);
}

void PCDMWidget::showVisualization()
{
    if (!m_project)
    {
        return;
    }

    m_visGenerator->openRenderView();

    if (!m_lastModelTimestamp.isValid())
    {
        return;
    }

    auto model = m_project->model(m_lastModelTimestamp);
    if (!model)
    {
        return;
    }

    m_visGenerator->showModel(*model);
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
    if (!m_project)
    {
        m_ui->savedModelsTable->setRowCount(0);
        m_ui->savedModelProperties->clear();
        return;
    }

    auto && models = m_project->models();
    const int numModels = models.size() > static_cast<size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max() : static_cast<int>(models.size());

    m_ui->savedModelsTable->setRowCount(numModels);
    
    auto it = models.begin();
    for (int i = 0; i < numModels; ++i)
    {
        auto timestampItem = new QTableWidgetItem();
        timestampItem->setData(Qt::DisplayRole, it->first);
        m_ui->savedModelsTable->setItem(i, 0, timestampItem);
        m_ui->savedModelsTable->setItem(i, 1, new QTableWidgetItem(it->second->name()));
    }

    m_ui->savedModelsTable->resizeColumnsToContents();
    m_ui->savedModelsTable->resizeRowsToContents();
}
