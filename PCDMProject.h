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

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QDateTime>
#include <QObject>

#include <vtkSmartPointer.h>

#include <core/CoordinateSystems_fwd.h>

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
     * invalidated.
     * ReferenceCoordinateSystemSpecification will be read from the data set's field data, if
     * available.
     */
    bool importHorizontalCoordinatesFrom(vtkDataSet & dataSet);
    vtkDataSet * horizontalCoordinatesDataSet();
    const std::array<std::vector<pCDM::t_FP>, 2> & horizontalCoordinateValues();
    const QString & horizontalCoordinatesGeometryType() const;
    size_t numHorizontalCoordinates() const;
    /**
     * Read coordinate system specifications from the coordinate data set's field data.
     */
    ReferencedCoordinateSystemSpecification coordinateSystem() const;

    /** Set the Poisson's ratio. Changing nu invalidates previous modeling results. */
    void setPoissonsRatio(pCDM::t_FP nu);
    pCDM::t_FP poissonsRatio() const;

    const std::map<QDateTime, std::unique_ptr<PCDMModel>> & models() const;
    PCDMModel * addModel(const QDateTime & timestamp = QDateTime::currentDateTime());
    bool deleteModel(const QDateTime & timestamp);

    PCDMModel * model(const QDateTime & timestamp);
    const PCDMModel * model(const QDateTime & timestamp) const;

    /** Store the timestamp of a specific model, e.g., the last one the user worked with. */
    const QDateTime & lastModelTimestamp() const;
    void setLastModelTimestamp(const QDateTime & timestamp);

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
    QString m_coordsGeometryType;

    std::map<QDateTime, std::unique_ptr<PCDMModel>> m_models;
    QDateTime m_lastModelTimestamp;

    pCDM::t_FP m_nu;
};
