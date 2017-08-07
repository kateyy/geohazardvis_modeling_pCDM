#pragma once

#include <memory>

#include <QDialog>


class Ui_PCDMCreateProjectDialog;


class PCDMCreateProjectDialog : public QDialog
{
public:
    explicit PCDMCreateProjectDialog(QWidget * parent = nullptr, Qt::WindowFlags f = {});
    ~PCDMCreateProjectDialog() override;

    bool isValid() const;

    void setProjectName(const QString & name);
    QString projectName() const;
    void setBaseDir(const QString & baseDir);
    QString baseDir() const;

    static QString getNewProjectPath(QWidget * parent,
        const QString & baseDir = {},
        const QString & projectName = {});

private:
    void evaluate();

private:
    std::unique_ptr<Ui_PCDMCreateProjectDialog> m_ui;
    bool m_isValid;
};
