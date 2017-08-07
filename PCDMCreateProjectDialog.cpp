#include "PCDMCreateProjectDialog.h"
#include "ui_PCDMCreateProjectDialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

#include <core/io/io_helper.h>


PCDMCreateProjectDialog::PCDMCreateProjectDialog(QWidget * parent, Qt::WindowFlags f)
    : QDialog(parent, f)
    , m_ui{ std::make_unique<Ui_PCDMCreateProjectDialog>() }
    , m_isValid{ false }
{
    m_ui->setupUi(this);

    connect(m_ui->projectBaseDirButton, &QAbstractButton::clicked, [this] () {
        const auto dir = QFileDialog::getExistingDirectory(this, "Open Base Directory");
        if (!dir.isEmpty())
        {
            m_ui->projectBaseDirEdit->setText(dir);
        }
    });

    connect(m_ui->cancelButton, &QAbstractButton::clicked,
        this, &PCDMCreateProjectDialog::reject);
    connect(m_ui->okayButton, &QAbstractButton::clicked,
        this, &PCDMCreateProjectDialog::evaluate);
}

PCDMCreateProjectDialog::~PCDMCreateProjectDialog() = default;

bool PCDMCreateProjectDialog::isValid() const
{
    return m_isValid;
}

void PCDMCreateProjectDialog::setProjectName(const QString & name)
{
    m_ui->projectNameEdit->setText(name);
}

QString PCDMCreateProjectDialog::projectName() const
{
    return m_ui->projectNameEdit->text();
}

void PCDMCreateProjectDialog::setBaseDir(const QString & baseDir)
{
    m_ui->projectBaseDirEdit->setText(baseDir);
}

QString PCDMCreateProjectDialog::baseDir() const
{
    return m_ui->projectBaseDirEdit->text();
}

QString PCDMCreateProjectDialog::getNewProjectPath(QWidget * parent,
    const QString & baseDir, const QString & projectName)
{
    PCDMCreateProjectDialog self{ parent };
    self.setBaseDir(baseDir);
    self.setProjectName(projectName);
    if (!self.exec()) {
        return {};
    }
    if (!self.isValid()) {
        return {};
    }
    return QDir(self.baseDir()).filePath(self.projectName());
}

void PCDMCreateProjectDialog::evaluate()
{
    m_isValid = false;

    auto msg = [this] (const QString & text) { QMessageBox::warning(this, "", text); };

    if (m_ui->projectNameEdit->text().isEmpty())
    {
        msg("Please enter a project name.");
        return;
    }

    QChar invalidChar;
    if (!io::isFileNameNormalized(m_ui->projectNameEdit->text(), &invalidChar))
    {
        msg(QString("Project name contains invalid character: %1").arg(invalidChar));
        return;
    }

    if (m_ui->projectBaseDirEdit->text().isEmpty())
    {
        msg("Please enter a base directory path.");
        return;
    }

    const QFileInfo baseDirInfo{ m_ui->projectBaseDirEdit->text() };
    if (!baseDirInfo.exists())
    {
        msg("Please enter the path to an existing base directory.");
        return;
    }

    if (!baseDirInfo.isDir())
    {
        msg("The entered base path is not a directory.");
        return;
    }

    const QDir baseDir{ baseDirInfo.filePath() };
    if (baseDir.exists(m_ui->projectNameEdit->text()))
    {
        msg(QString("The folder \"%1\" already exists in the base directory.")
            .arg(m_ui->projectNameEdit->text()));
        return;
    }

    m_isValid = true;

    accept();
}
