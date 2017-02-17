#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QDateTime>
#include <QObject>

#include <vtkSmartPointer.h>

#include "pCDM_types.h"


class QSettings;
class vtkDataSet;

class PCDMModel;


class PCDMProject : public QObject
{
    Q_OBJECT

public:
    explicit PCDMProject(const QString & rootFolder);
    ~PCDMProject() override;

    const QString & rootFolder() const;

    static const QString & projectFileNameFilter();
    static bool checkFolderIsProject(const QString & rootFolder, QString * errorMessage = nullptr);

    /**
     * Import coordinates (structure) of the data set into the project.
     * @return true after successfully fetching coordinates from the data set. Only if true is
     * returned, project settings and data is modified. In this case, previous modeling results are
     * invalidated. */
    bool importHorizontalCoordinatesFrom(vtkDataSet & dataSet);
    vtkDataSet * horizontalCoordinates();
    const std::array<std::vector<pCDM::t_FP>, 2> & horizontalCoordinateValues();

    /** Set the Poisson's ratio. Changing nu invalidates previous modeling results. */
    void setPoissonsRatio(pCDM::t_FP nu);
    pCDM::t_FP poissonsRatio() const;

    const std::map<QDateTime, std::unique_ptr<PCDMModel>> & models() const;
    PCDMModel * addModel(const QDateTime & timestamp = QDateTime::currentDateTime());
    bool deleteModel(const QDateTime & timestamp);

    PCDMModel * model(const QString & timestamp);
    const PCDMModel * model(const QString & timestamp) const;
    PCDMModel * model(const QDateTime & timestamp);
    const PCDMModel * model(const QDateTime & timestamp) const;

    /** Generate a string representation of a QDateTime that can be used is file name */
    static QString timestampToString(const QDateTime & timestamp);
    /** Parse a timestamp encoded as string and create a QTimeStamp */
    static QDateTime stringToTimestamp(const QString & timestamp);

signals:
    void horizontalCoordinatesChanged();

private:
    void readModels();
    void readCoordinates();

    void accessSettings(std::function<void(QSettings &)> func);
    void readSettings(std::function<void(const QSettings &)> func);

    void invalidateModels();

private:
    const QString m_rootFolder;
    const QString m_projectFileName;
    const QString m_modelsDir;

    vtkSmartPointer<vtkDataSet> m_coordsDataSet;
    std::array<std::vector<pCDM::t_FP>, 2> m_horizontalCoordsValues;

    std::map<QDateTime, std::unique_ptr<PCDMModel>> m_models;

    pCDM::t_FP m_nu;
};
