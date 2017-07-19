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

#include "PCDMModel.h"

#include <algorithm>
#include <cassert>

#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QtConcurrent>

#include <core/data_objects/DataObject.h>
#include <core/io/BinaryFile.h>
#include <core/utility/conversions.h>

#include "PCDMBackend.h"
#include "PCDMProject.h"


namespace
{

using t_FP = pCDM::t_FP;

QString settingsFileName(const QString & baseDir, const QDateTime & timestamp)
{
    return QDir(baseDir).filePath(PCDMProject::timestampToString(timestamp) + ".ini");
}

QString resultsFileName(const QString & baseDir, const QDateTime & timestamp)
{
    return QDir(baseDir).filePath(PCDMProject::timestampToString(timestamp) + "_u_vec.bin");
}

}


PCDMModel::PCDMModel(
    PCDMProject & project,
    const QDateTime & timestamp,
    const QString & baseDir)
    : QObject()
    , m_project{ project }
    , m_baseDir{ baseDir }
    , m_isRemoved{ false }
    , m_hasStoredResults{ false }
    , m_timestamp{ timestamp }
    , m_name{}
    , m_parameters{}
    , m_results{}
    , m_resultDataObject{}
{
    if (!parametersFromFile())
    {
        // The parameters file did not exist yet. Write default values to the file.
        parametersToFile();
    }

    readSettings([this] (const QSettings & settings)
    {
        m_name = settings.value("Name").toString();
        m_hasStoredResults = settings.value("HasStoredResults").toBool();
    });

    connect(&m_computeFutureWatcher, &QFutureWatcher<void>::finished,
        this, &PCDMModel::requestCompleted);
}

PCDMModel::~PCDMModel()
{
    if (m_isRemoved)
    {
        return;
    }

    parametersToFile();

    accessSettings([this] (QSettings & settings)
    {
        settings.setValue("Name", m_name);
    });
}

PCDMProject & PCDMModel::project()
{
    return m_project;
}

const PCDMProject & PCDMModel::project() const
{
    return m_project;
}

const QDateTime & PCDMModel::timestamp() const
{
    return m_timestamp;
}

const QString & PCDMModel::name() const
{
    return m_name;
}

void PCDMModel::setName(const QString & name)
{
    if (m_name == name)
    {
        return;
    }

    m_name = name;

    accessSettings([this] (QSettings & settings)
    {
        settings.setValue("Name", m_name);
    });

    emit nameChanged(m_name);
}

bool PCDMModel::isValid() const
{
    return !m_isRemoved && m_timestamp.isValid();
}

bool PCDMModel::hasResults() const
{
    return loadedResultsAreValid() || m_hasStoredResults;
}

void PCDMModel::setParameters(const pCDM::PointCDMParameters & sourceParameters)
{
    if (m_parameters == sourceParameters)
    {
        return;
    }

    m_parameters = sourceParameters;

    invalidateResults();

    parametersToFile();
}

const pCDM::PointCDMParameters & PCDMModel::parameters() const
{
    return m_parameters;
}

void PCDMModel::requestResultsAsync()
{
    waitForResults();

    struct EmitOnExit
    {
        explicit EmitOnExit(PCDMModel & p) : p{ p } , skipEmit{ false } { }
        ~EmitOnExit()
        {
            if (!skipEmit)
            {
                emit p.requestCompleted();
            }
        }

        PCDMModel & p;
        bool skipEmit;
    } emitOnExit(*this);

    if (loadedResultsAreValid())
    {
        return;
    }

    if (m_hasStoredResults)
    {
        readResults();
        if (loadedResultsAreValid())
        {
            return;
        }
    }

    auto runFunc = [this] ()
    {
        PCDMBackend backend;
        backend.setHorizontalCoords(m_project.horizontalCoordinateValues());
        backend.setParameters({ m_parameters, m_project.poissonsRatio() });

        backend.run();

        if (backend.state() != PCDMBackend::State::resultsReady)
        {
            invalidateResults();
            return;
        }

        m_results = std::move(backend.takeResults());

        storeResults();
    };

    m_computeFutureWatcher.setFuture(QtConcurrent::run(runFunc));

    emitOnExit.skipEmit = true;
}

bool PCDMModel::waitForResults()
{
    m_computeFutureWatcher.waitForFinished();

    return loadedResultsAreValid();
}

const std::array<std::vector<pCDM::t_FP>, 3> & PCDMModel::results()
{
    if (m_results[0].empty() && m_hasStoredResults)
    {
        readResults();
    }

    return m_results;
}

void PCDMModel::invalidateResults()
{
    for (auto & r : m_results)
    {
        r.clear();
    }

    QFile(resultsFileName(m_baseDir, m_timestamp)).remove();

    m_hasStoredResults = false;
    writeHasStoredResults();
}

void PCDMModel::prepareDelete()
{
    m_isRemoved = true;

    invalidateResults();

    QFile(settingsFileName(m_baseDir, m_timestamp)).remove();
}

bool PCDMModel::accessSettings(std::function<void(QSettings &)> func) const
{
    const auto fileName = settingsFileName(m_baseDir, m_timestamp);

    QSettings settings(fileName, QSettings::IniFormat);
    func(settings);
    settings.sync();

    return settings.status() == QSettings::NoError;
}

bool PCDMModel::readSettings(std::function<void(const QSettings &)> func) const
{
    const auto fileName = settingsFileName(m_baseDir, m_timestamp);

    if (!QFile(fileName).exists())
    {
        return false;
    }

    const QSettings settings(fileName, QSettings::IniFormat);
    func(settings);

    return true;
}

bool PCDMModel::parametersFromFile()
{
    return readSettings([this] (const QSettings & settings)
    {
        m_parameters.horizontalCoord = stringToArray<t_FP, 2>(
            settings.value("PointCDM/HorizontalCoordinate").toString());
        m_parameters.depth = settings.value("PointCDM/Depth").value<t_FP>();
        m_parameters.omega = stringToArray<t_FP, 3>(
            settings.value("PointCDM/Rotation").toString());
        m_parameters.dv = stringToArray<t_FP, 3>(
            settings.value("PointCDM/Potencies").toString());
    });
}

bool PCDMModel::parametersToFile() const
{
    return accessSettings([this] (QSettings & settings)
    {
        settings.setValue("PointCDM/HorizontalCoordinate", arrayToString(m_parameters.horizontalCoord));
        settings.setValue("PointCDM/Depth", m_parameters.depth);
        settings.setValue("PointCDM/Rotation", arrayToString(m_parameters.omega));
        settings.setValue("PointCDM/Potencies", arrayToString(m_parameters.dv));
    });
}

void PCDMModel::readResults()
{
    const auto fileName = resultsFileName(m_baseDir, m_timestamp);

    if (!m_hasStoredResults)
    {
        QFile(fileName).remove();
        return;
    }

    auto failDiscardData = [this, &fileName] ()
    {
        qDebug() << "Reading previously stored results failed. Discarding data.";
        QFile(fileName).remove();
        m_hasStoredResults = false;
        writeHasStoredResults();
    };

    const auto numTuples = m_project.numHorizontalCoordinates();

    auto reader = BinaryFile(fileName, BinaryFile::OpenMode::Read);

    for (auto & component : m_results)
    {
        if (!reader.read(numTuples, component))
        {
            return failDiscardData();
        }
    }
}

void PCDMModel::storeResults()
{
    const auto fileName = resultsFileName(m_baseDir, m_timestamp);

    const auto numTuples = m_project.numHorizontalCoordinates();
    assert(!m_results[0].empty());
    if (m_results[0].empty()
        || !std::all_of(m_results.begin(), m_results.end(),
            [numTuples] (const decltype(m_results)::value_type & vec)
        { return vec.size() == numTuples; }))
    {
        qDebug() << "Trying to write invalid results";
        return;
    }

    auto setHasSuccess = [this, &fileName] (bool success)
    {
        m_hasStoredResults = success;
        if (!success)
        {
            qDebug() << "Failed to write results file:" << fileName;
            QFile(fileName).remove();
        }
        writeHasStoredResults();
    };

    auto writer = BinaryFile(fileName, BinaryFile::OpenMode::Write | BinaryFile::OpenMode::Truncate);
    for (auto & component : m_results)
    {
        if (!writer.write(component))
        {
            return setHasSuccess(false);
        }
    }

    return setHasSuccess(true);
}

bool PCDMModel::loadedResultsAreValid() const
{
    const auto numTuples = m_project.numHorizontalCoordinates();

    return !m_results[0].empty()
        && std::all_of(m_results.begin(), m_results.end(),
            [numTuples] (const decltype(m_results)::value_type & vec)
                { return vec.size() == numTuples; });
}

void PCDMModel::writeHasStoredResults() const
{
    accessSettings([this] (QSettings & settings)
    {
        settings.setValue("HasStoredResults", m_hasStoredResults);
    });
}
