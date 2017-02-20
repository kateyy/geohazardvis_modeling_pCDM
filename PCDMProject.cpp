#include "PCDMProject.h"

#include <algorithm>
#include <cassert>

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <vtkAOSDataArrayTemplate.h>
#include <vtkDataSet.h>
#include <vtkDelimitedTextWriter.h>
#include <vtkImageData.h>
#include <vtkPolyData.h>
#include <vtkTable.h>
#include <vtkVector.h>

#include <core/CoordinateSystems.h>
#include <core/io/TextFileReader.h>
#include <core/utility/conversions.h>
#include <core/utility/DataExtent.h>
#include <core/utility/vtkvectorhelper.h>

#include "PCDMModel.h"


using pCDM::t_FP;


namespace
{

using Vector3 = ::vtkVector3<t_FP>;

QString projectFileName(const QString & rootFolder)
{
    return QDir(rootFolder).filePath(PCDMProject::projectFileNameFilter());
}

QString coordsFileName(const QString & rootFolder)
{
    return QDir(rootFolder).filePath("Coordinates.txt");
}

QString modelsDir(const QString & rootFolder)
{
    return QDir(rootFolder).filePath("models");
}

}


PCDMProject::PCDMProject(const QString & rootFolder)
    : QObject()
    , m_rootFolder{ rootFolder }
    , m_projectFileName{ projectFileName(rootFolder) }
    , m_modelsDir{ modelsDir(rootFolder) }
{
    {   // touch the project file if it doesn't exist
        QFile projectFile(m_projectFileName);
        if (!projectFile.exists())
        {
            projectFile.open(QIODevice::ReadWrite);
        }
    }

    readSettings([this] (const QSettings & settings)
    {
        m_lastModelTimestamp = settings.value("MostRecentlyUsedModel").toDateTime();
        m_nu = settings.value("Material/nu").value<t_FP>();
    });

    readCoordinates();

    readModels();
}

PCDMProject::~PCDMProject() = default;

const QString & PCDMProject::rootFolder() const
{
    return m_rootFolder;
}

const QString & PCDMProject::projectFileNameFilter()
{
    static const QString fileName = "pCDM_project.ini";
    return fileName;
}

bool PCDMProject::checkFolderIsProject(const QString & rootFolder, QString * errorMessage)
{
    const QFileInfo file(projectFileName(rootFolder));
    const QFileInfo dir(rootFolder);

    auto setMsg = [errorMessage] (const QString & message)
    {
        if (errorMessage)
        {
            *errorMessage = message;
        }

        return false;
    };

    if (!file.exists())
    {
        return setMsg("The selected folder does not contain a project file.");
    }
    if (!dir.isReadable() || !file.isReadable())
    {
        return setMsg("The project is not readable. Please check your access rights.");
    }
    if (!dir.isWritable() || !file.isWritable())
    {
        return setMsg("The project is not writable. Please check your access rights.");
    }

    return true;
}

bool PCDMProject::importHorizontalCoordinatesFrom(vtkDataSet & dataSet)
{
    const auto fileName = coordsFileName(m_rootFolder);

    if (dataSet.GetNumberOfPoints() == 0)
    {
        return false;
    }

    auto applyNewDataSet = [this, &dataSet] (vtkDataSet & newDataSet, const QString & dataTypeString)
    {
        invalidateModels();

        for (auto & vec : m_horizontalCoordsValues)
        {
            vec.clear();
        }

        m_coordsDataSet = &newDataSet;
        m_coordsGeometryType = dataTypeString;
        const auto coordsSpec = ReferencedCoordinateSystemSpecification::fromFieldData(*dataSet.GetFieldData());
        coordsSpec.writeToFieldData(*newDataSet.GetFieldData());

        accessSettings([&dataTypeString, &coordsSpec] (QSettings & settings)
        {
            settings.beginGroup("Surface");
            settings.setValue("ValidData", true);
            settings.setValue("DataType", dataTypeString);
            QVariant coordsVariant;
            coordsVariant.setValue(coordsSpec);
            settings.setValue("CoordinateSystem", coordsVariant);
        });

        emit horizontalCoordinatesChanged();

        return true;
    };

    if (auto sourceImagePtr = vtkImageData::SafeDownCast(&dataSet))
    {
        auto & sourceImage = *sourceImagePtr;
        auto newImage = vtkSmartPointer<vtkImageData>::New();
        auto & image = *newImage;

        // Eliminate elevations in the structure

        vtkVector3d origin;
        sourceImage.GetOrigin(origin.GetData());
        origin.SetZ(0.0);
        image.SetOrigin(origin.GetData());

        ImageExtent extent;
        sourceImage.GetExtent(extent.data());
        extent.setDimension(2, 0, 0);
        image.SetExtent(extent.data());

        vtkVector3d spacing;
        sourceImage.GetSpacing(spacing.GetData());
        spacing.SetZ(1.0);  // just some default value (must not be 0)
        image.SetSpacing(spacing.GetData());

        QFile::remove(fileName);
        QSettings imageSpec(fileName, QSettings::IniFormat);
        imageSpec.beginGroup("Grid");
        imageSpec.setValue("Origin", vectorToString(convertTo<2>(origin)));
        imageSpec.setValue("Extent", arrayToString(std::array<int, 4>(extent.convertTo<2>())));
        imageSpec.setValue("Spacing", vectorToString(convertTo<2>(spacing)));

        return applyNewDataSet(image, "Regular Grid");
    }

    if (auto sourcePolyPtr = vtkPolyData::SafeDownCast(&dataSet))
    {
        auto & sourcePoly = *sourcePolyPtr;
        auto sourcePoints = sourcePoly.GetPoints()->GetData();
        assert(sourcePoints->GetNumberOfComponents() == 3);
        const auto numPoints = sourcePoints->GetNumberOfTuples();
        assert(numPoints > 0);

        // Copy horizontal coordinates into a new data set used for visualization

        auto newPoly = vtkSmartPointer<vtkPolyData>::New();
        auto & poly = *newPoly;

        auto newPoints = vtkSmartPointer<vtkAOSDataArrayTemplate<t_FP>>::New();
        newPoints->SetNumberOfComponents(3);
        newPoints->SetNumberOfTuples(numPoints);
        newPoints->CopyComponent(0, sourcePoints, 0);
        newPoints->CopyComponent(1, sourcePoints, 1);
        // Eliminate elevations
        newPoints->FillComponent(2, 0.0);

        auto points = vtkSmartPointer<vtkPoints>::New();
        points->SetData(newPoints);
        poly.SetPoints(points);

        auto pointIds = vtkSmartPointer<vtkIdList>::New();
        pointIds->SetNumberOfIds(numPoints);
        std::iota(
            pointIds->GetPointer(0),
            pointIds->GetPointer(numPoints),
            0);
        auto verts = vtkSmartPointer<vtkCellArray>::New();
        verts->InsertNextCell(pointIds);
        poly.SetVerts(verts);


        // Copy X/Y columns to be used in the modeling backend

        m_horizontalCoordsValues[0].resize(static_cast<size_t>(numPoints));
        m_horizontalCoordsValues[1].resize(static_cast<size_t>(numPoints));

        // Map backend data into the VTK arrays for text export
        auto x = vtkSmartPointer<vtkAOSDataArrayTemplate<t_FP>>::New();
        auto y = vtkSmartPointer<vtkAOSDataArrayTemplate<t_FP>>::New();
        x->SetName("X");
        y->SetName("Y");
        x->SetArray(m_horizontalCoordsValues[0].data(), numPoints, 1);
        y->SetArray(m_horizontalCoordsValues[1].data(), numPoints, 1);
        x->CopyComponent(0, newPoints, 0);
        y->CopyComponent(0, newPoints, 1);

        auto table = vtkSmartPointer<vtkTable>::New();
        table->AddColumn(x);
        table->AddColumn(y);

        auto writer = vtkSmartPointer<vtkDelimitedTextWriter>::New();
        writer->SetFieldDelimiter(" ");
        writer->SetUseStringDelimiter(false);
        writer->SetInputData(table);
        writer->SetFileName(fileName.toUtf8().data());
        if (writer->Write() != 1)
        {
            return false;
        }

        return applyNewDataSet(poly, "Point Cloud");
    }

    return false;
}

vtkDataSet * PCDMProject::horizontalCoordinatesDataSet()
{
    return m_coordsDataSet;
}

const std::array<std::vector<pCDM::t_FP>, 2> & PCDMProject::horizontalCoordinateValues()
{
    if (!m_horizontalCoordsValues[0].empty())
    {
        return m_horizontalCoordsValues;
    }

    if (!m_coordsDataSet)
    {
        return m_horizontalCoordsValues;
    }

    const auto numCoords = static_cast<size_t>(m_coordsDataSet->GetNumberOfPoints());
    for (auto & vec : m_horizontalCoordsValues)
    {
        vec.resize(numCoords);
    }

    for (size_t i = 0; i < numCoords; ++i)
    {
        std::array<t_FP, 3> point;
        m_coordsDataSet->GetPoint(static_cast<vtkIdType>(i), point.data());
        m_horizontalCoordsValues[0][i] = point[0];
        m_horizontalCoordsValues[1][i] = point[1];
    }

    return m_horizontalCoordsValues;
}

const QString & PCDMProject::horizontalCoordinatesGeometryType() const
{
    return m_coordsGeometryType;
}

ReferencedCoordinateSystemSpecification PCDMProject::coordinateSystem() const
{
    ReferencedCoordinateSystemSpecification spec;

    if (!m_coordsDataSet)
    {
        return spec;
    }

    spec.readFromFieldData(*m_coordsDataSet->GetFieldData());

    return spec;
}

void PCDMProject::setPoissonsRatio(pCDM::t_FP nu)
{
    if (nu == m_nu)
    {
        return;
    }

    m_nu = nu;

    invalidateModels();

    accessSettings([nu] (QSettings & settings)
    {
        settings.beginGroup("Material");
        settings.setValue("nu", nu);
    });
}

pCDM::t_FP PCDMProject::poissonsRatio() const
{
    return m_nu;
}

const std::map<QDateTime, std::unique_ptr<PCDMModel>> & PCDMProject::models() const
{
    return m_models;
}

PCDMModel * PCDMProject::addModel(const QDateTime & timestamp)
{
    auto it = m_models.find(timestamp);
    if (it != m_models.end())
    {
        return it->second.get();
    }

    const auto modelPath = modelsDir(m_rootFolder);

    auto modelDir = QDir(modelPath);
    if (!modelDir.exists())
    {
        const auto dirName = modelDir.dirName();
        auto upDir = modelDir;
        upDir.cdUp();
        if (!upDir.mkdir(dirName))
        {
            return nullptr;
        }
    }

    auto newIt = m_models.emplace(
        timestamp,
        std::make_unique<PCDMModel>(
            *this,
            timestamp,
            modelPath));

    return newIt.first->second.get();
}

bool PCDMProject::deleteModel(const QDateTime & timestamp)
{
    const auto it = m_models.find(timestamp);
    if (it == m_models.end())
    {
        return false;
    }

    it->second->prepareDelete();
    m_models.erase(it);

    return true;
}

PCDMModel * PCDMProject::model(const QDateTime & timestamp)
{
    const auto it = m_models.find(timestamp);
    if (it == m_models.end())
    {
        return nullptr;
    }

    return it->second.get();
}

const PCDMModel * PCDMProject::model(const QDateTime & timestamp) const
{
    const auto it = m_models.find(timestamp);
    if (it == m_models.end())
    {
        return nullptr;
    }

    return it->second.get();
}

const QDateTime & PCDMProject::lastModelTimestamp() const
{
    return m_lastModelTimestamp;
}

void PCDMProject::setLastModelTimestamp(const QDateTime & timestamp)
{
    if (m_lastModelTimestamp == timestamp)
    {
        return;
    }

    m_lastModelTimestamp = timestamp;

    accessSettings([&timestamp] (QSettings & settings)
    {
        settings.setValue("MostRecentlyUsedModel", timestamp);
    });
}

QString PCDMProject::timestampToString(const QDateTime & timestamp)
{
    assert(timestamp.isValid());

    return timestamp.toString("yyyy-MM-dd HH-mm-ss.zzz");
}

QDateTime PCDMProject::stringToTimestamp(const QString & timestamp)
{
    return QDateTime::fromString(timestamp, "yyyy-MM-dd HH-mm-ss.zzz");
}

void PCDMProject::readModels()
{
    const auto entries = QDir(modelsDir(m_rootFolder)).entryInfoList(
        { "*.ini" }, QDir::Filter::Files | QDir::Readable | QDir::Writable);

    decltype(m_models) newModels;

    for (auto && file : entries)
    {
        const auto timestamp = stringToTimestamp(file.completeBaseName());
        if (!timestamp.isValid())
        {
            continue;
        }

        auto model = std::make_unique<PCDMModel>(
            *this,
            timestamp,
            m_modelsDir);

        if (!model->isValid())
        {
            continue;
        }

        newModels.emplace(timestamp, std::move(model));
    }

    std::swap(m_models, newModels);
}

void PCDMProject::readCoordinates()
{
    const auto fileName = coordsFileName(m_rootFolder);

    bool hasValidData = false;
    bool isGrid = false;
    bool isPoints = false;
    QString geometryType;

    ReferencedCoordinateSystemSpecification coordsSpec;

    readSettings([&hasValidData, &isGrid, &isPoints, &geometryType, &coordsSpec]
    (const QSettings & settings)
    {
        if (!settings.value("Surface/ValidData", false).toBool())
        {
            return;
        }
        hasValidData = true;

        coordsSpec =
            settings.value("Surface/CoordinateSystem").value<ReferencedCoordinateSystemSpecification>();

        geometryType = settings.value("Surface/DataType").toString();
        if (geometryType.isEmpty())
        {
            return;
        }
        if (geometryType == "Regular Grid")
        {
            isGrid = true;
            return;
        }
        if (geometryType == "Point Cloud")
        {
            isPoints = true;
            return;
        }
    });

    auto setToInvalid = [this, &fileName] ()
    {
        accessSettings([] (QSettings & settings)
        {
            settings.beginGroup("Surface");
            settings.setValue("ValidData", false);
            settings.remove("DataType");
        });
        m_coordsDataSet = {};
        m_coordsGeometryType.clear();
        QFile(fileName).remove();
    };

    if (!hasValidData || (!isGrid && !isPoints))
    {
        setToInvalid();
        return;
    }

    auto setToValid = [this, &geometryType, &coordsSpec] (vtkDataSet & dataSet)
    {
        m_coordsGeometryType = geometryType;
        m_coordsDataSet = &dataSet;
        coordsSpec.writeToFieldData(*dataSet.GetFieldData());
    };

    if (isGrid)
    {
        const QSettings imageSpec(fileName, QSettings::IniFormat);
        if (!imageSpec.contains("Grid/Origin")
            || !imageSpec.contains("Grid/Extent")
            || !imageSpec.contains("Grid/Spacing"))
        {
            setToInvalid();
            return;
        }
        const auto originXY = stringToVector2<double>(imageSpec.value("Grid/Origin").toString());
        const auto extentXY = DataExtent<int, 2>(stringToArray<int, 4>(
            imageSpec.value("Grid/Extent").toString()));
        const auto spacingXY = stringToVector2<double>(imageSpec.value("Grid/Spacing").toString());

        if (extentXY.isEmpty() || spacingXY[0] <= 0.0 || spacingXY[1] <= 0.0)
        {
            setToInvalid();
            return;
        }

        auto origin = convertTo<3>(originXY, 0.0);
        auto extent = extentXY.convertTo<3>();
        extent.setDimension(2, 0, 0);
        auto spacing = convertTo<3>(spacingXY, 1.0);

        auto image = vtkSmartPointer<vtkImageData>::New();
        image->SetOrigin(origin.GetData());
        image->SetExtent(extent.data());
        image->SetSpacing(spacing.GetData());

        setToValid(*image);

        return;
    }

    if (isPoints)
    {
        TextFileReader::Vector_t<t_FP> coords;

        auto reader = TextFileReader(fileName);
        TextFileReader::Vector_t<QString> header;
        reader.read(header, 1); // skip the header written by vtkDelimitedTextWriter
        reader.read(coords);
        if (!reader.stateFlags().testFlag(TextFileReader::successful)
            || (coords.size() != 2) || coords.front().empty())
        {
            setToInvalid();
            return;
        }

        const auto numPoints = static_cast<vtkIdType>(coords.front().size());

        // TODO enable SOA Array dispatch in VTK build and try if it has performance benefits
        /*auto pointsData = vtkSmartPointer<vtkSOADataArrayTemplate<t_FP>>::New();
        pointsData->SetNumberOfComponents(3);
        pointsData->SetNumberOfTuples(numPoints);
        for (int c = 0; c < 2; ++c)
        {
            std::copy(coords[c].begin(), coords[c].end(), pointsData->GetComponentArrayPointer(c));
        }
        std::fill_n(pointsData->GetComponentArrayPointer(2),
            coords[0].size(),
            static_cast<t_FP>(0.f));*/

        auto pointsData = vtkSmartPointer<vtkAOSDataArrayTemplate<t_FP>>::New();
        pointsData->SetNumberOfComponents(3);
        pointsData->SetNumberOfTuples(numPoints);
        for (int c = 0; c < 2; ++c)
        {
            for (vtkIdType i = 0; i < numPoints; ++i)
            {
                pointsData->SetTypedComponent(i, c,
                    coords[static_cast<size_t>(c)][static_cast<size_t>(i)]);
            }
        }
        pointsData->FillTypedComponent(2, static_cast<t_FP>(0));

        auto points = vtkSmartPointer<vtkPoints>::New();
        points->SetData(pointsData);

        auto pointIds = vtkSmartPointer<vtkIdList>::New();
        pointIds->SetNumberOfIds(numPoints);
        std::iota(
            pointIds->GetPointer(0),
            pointIds->GetPointer(numPoints),
            0);
        auto verts = vtkSmartPointer<vtkCellArray>::New();
        verts->InsertNextCell(pointIds);

        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(points);
        poly->SetVerts(verts);

        setToValid(*poly);

        return;
    }

    setToInvalid();
}

void PCDMProject::accessSettings(std::function<void(QSettings &)> func)
{
    QSettings settings(m_projectFileName, QSettings::IniFormat);
    func(settings);
}

void PCDMProject::readSettings(std::function<void(const QSettings &)> func)
{
    const QSettings settings(m_projectFileName, QSettings::IniFormat);
    func(settings);
}

void PCDMProject::invalidateModels()
{
    for (const auto & p : m_models)
    {
        p.second->invalidateResults();
    }
}
