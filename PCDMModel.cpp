#include "PCDMModel.h"

#include <algorithm>
#include <array>
#include <cassert>

#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QtConcurrent>

#include <vtkAOSDataArrayTemplate.h>
#include <vtkDelimitedTextWriter.h>
#include <vtkSmartPointer.h>
#include <vtkTable.h>

#include <core/data_objects/DataObject.h>
#include <core/io/TextFileReader.h>
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
    return QDir(baseDir).filePath(PCDMProject::timestampToString(timestamp) + "_u_vec.ini");
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

DataObject * PCDMModel::resultDataObject()
{
    // TODO
    return nullptr;
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

    auto reader = TextFileReader(fileName);
    TextFileReader::Vector_t<QString> header;
    reader.read(header, 1);
    TextFileReader::Vector_t<t_FP> storedResults;
    reader.read(storedResults);

    if (!reader.stateFlags().testFlag(TextFileReader::successful)
        || storedResults.size() != m_results.size())
    {
        qDebug() << "Reading previously stored results failed. Discarding data.";
        QFile(fileName).remove();
        m_hasStoredResults = false;
        writeHasStoredResults();
        return;
    }

    for (size_t i = 0; i < m_results.size(); ++i)
    {
        m_results[i] = std::move(storedResults[i]);
    }
}

void PCDMModel::storeResults()
{
    const auto fileName = resultsFileName(m_baseDir, m_timestamp);

    assert(m_results.size() == 3);
    assert(!m_results[0].empty());

    const auto numValues = m_results[0].size();

    if (m_results[0].empty()
        || !std::all_of(m_results.begin(), m_results.end(),
            [numValues] (const decltype(m_results)::value_type & vec)
        { return vec.size() == numValues; }))
    {
        qDebug() << "Trying to write invalid results";
        return;
    }

    std::array<vtkSmartPointer<vtkAOSDataArrayTemplate<t_FP>>, 3> vtkArrays;
    auto table = vtkSmartPointer<vtkTable>::New();
    for (unsigned c = 0; c < 3; ++c)
    {
        auto & array = vtkArrays[c];
        array = vtkSmartPointer<vtkAOSDataArrayTemplate<t_FP>>::New();
        array->SetArray(
            m_results[c].data(),
            static_cast<vtkIdType>(numValues),
            true, true);
        table->AddColumn(array);
    }
    vtkArrays[0]->SetName("ue");
    vtkArrays[1]->SetName("un");
    vtkArrays[2]->SetName("uv");

    auto writer = vtkSmartPointer<vtkDelimitedTextWriter>::New();
    writer->SetFieldDelimiter(" ");
    writer->SetUseStringDelimiter(false);
    writer->SetFileName(fileName.toUtf8().data());
    writer->SetInputData(table);

    m_hasStoredResults = writer->Write() == 1;

    writeHasStoredResults();
}

bool PCDMModel::loadedResultsAreValid() const
{
    const auto numTuples = m_results[0].size();

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
