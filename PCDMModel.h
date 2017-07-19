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
#include <memory>
#include <vector>

#include <QDateTime>
#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include "pCDM_types.h"


class QSettings;

class DataObject;
class PCDMModel;
class PCDMProject;


struct PCDMParamTimestampLess
{
    PCDMParamTimestampLess();
    PCDMParamTimestampLess(const PCDMModel * lhs, const PCDMModel * rhs);
    PCDMParamTimestampLess(const PCDMModel & lhs, const PCDMModel & rhs);

    bool operator()() const;

    const PCDMModel * lhs;
    const PCDMModel * rhs;
};


class PCDMModel : public QObject
{
    Q_OBJECT

public:
    explicit PCDMModel(
        PCDMProject & project,
        const QDateTime & timestamp,
        const QString & baseDir);
    ~PCDMModel() override;

    PCDMProject & project();
    const PCDMProject & project() const;

    const QDateTime & timestamp() const;

    const QString & name() const;
    /**
     * Specify a user-defined name for the parametrization.
     * Parametrizations are uniquely identified by their timestamp, but names can be set,
     * simplifying handling for the user. */
    void setName(const QString & name);

    /**
     * @return true only if this instance is part of the project it refers to.
     * If false is returned, either this parametrization was removed from the project, or an
     * invalid timestamp was requested from the project.
     */
    bool isValid() const;

    /** @return whether valid results are computed for this parametrization. */
    bool hasResults() const;

    /**
     * Set parameters related to the point CDM.
     * Modifying parameters invalidates previously computed results.
     * Further parameters are the computing points and the Poisson's ratio. Those are defined in
     * the project and are equal for all parametrizations in a project.
     */
    void setParameters(const pCDM::PointCDMParameters & sourceParameters);
    const pCDM::PointCDMParameters & parameters() const;

    /**
     * Request to compute modeling results using the PCDMBackend.
     * This function first checks if results are already available and if the parameters are valid,
     * and then asynchronously triggers the computing process, if required.
     * In any case, requestCompleted() is emitted when all required steps are done.
     * Use hasResults() afterwards to check if the computation was successful.
     */
    void requestResultsAsync();
    /**
     * Block until results are available or an error occurred during the computation.
     * @return true only if valid outputs are available.
    */
    bool waitForResults();

    const std::array<std::vector<pCDM::t_FP>, 3> & results();

    void invalidateResults();

    void prepareDelete();

signals:
    void nameChanged(const QString & name);
    void requestCompleted();

private:
    /**
     * Read/Write access to the settings file.
     * @return whether syncing settings to the file did succeed.
     */
    bool accessSettings(std::function<void(QSettings &)> func) const;
    /**
     * Read access to the settings file.
     * @return false if the settings file does not exists.
     */
    bool readSettings(std::function<void(const QSettings &)> func) const;

    bool parametersFromFile();
    bool parametersToFile() const;

    void readResults();
    void storeResults();

    bool loadedResultsAreValid() const;
    void writeHasStoredResults() const;

private:
    PCDMProject & m_project;
    const QString m_baseDir;
    bool m_isRemoved;
    bool m_hasStoredResults;

    QDateTime m_timestamp;
    QString m_name;

    pCDM::PointCDMParameters m_parameters;

    QFutureWatcher<void> m_computeFutureWatcher;

    std::array<std::vector<pCDM::t_FP>, 3> m_results;
    std::unique_ptr<DataObject> m_resultDataObject;
};


inline PCDMParamTimestampLess::PCDMParamTimestampLess()
    : lhs{ nullptr }
    , rhs{ nullptr }
{
}

inline PCDMParamTimestampLess::PCDMParamTimestampLess(const PCDMModel * lhs, const PCDMModel * rhs)
    : lhs{ lhs }
    , rhs{ rhs }
{
}

inline PCDMParamTimestampLess::PCDMParamTimestampLess(const PCDMModel & lhs, const PCDMModel & rhs)
    : lhs{ &lhs }
    , rhs{ &rhs }
{
}

inline bool PCDMParamTimestampLess::operator()() const
{
    if (!lhs || !rhs)
    {
        return false;
    }

    return lhs->timestamp() < rhs->timestamp();
}
