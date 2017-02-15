
#define _USE_MATH_DEFINES
#include <cmath>

#include "PCDMBackend.h"

#include <algorithm>
#include <cassert>

#include <QDebug>

#include <Eigen/Geometry>


using pCDM::t_FP;


namespace
{

template<int rows, int cols> using Array = Eigen::Array<t_FP, rows, cols>;
template<int rows, int cols> using Matrix = Eigen::Matrix<t_FP, rows, cols>;
using ArrayX1 = Array<Eigen::Dynamic, 1>;
using ArrayX2 = Array<Eigen::Dynamic, 2>;
using ArrayX3 = Array<Eigen::Dynamic, 3>;
using Matrix2 = Matrix<2, 2>;
using Matrix2X = Matrix<2, Eigen::Dynamic>;
using Matrix3 = Matrix<3, 3>;
using MatrixX = Matrix<Eigen::Dynamic, Eigen::Dynamic>;
using MatrixX2 = Matrix<Eigen::Dynamic, 2>;
using Vector3 = Matrix<3, 1>;
using VectorX = Matrix<Eigen::Dynamic, 1>;

const auto pi = static_cast<t_FP>(M_PI);

/**
 * PTDdispSurf calculates surface displacements associated with a tensile
 * point dislocation(PTD) in an elastic half - space(Okada, 1985). */
ArrayX3 PTDdispSurf(
    const std::array<std::vector<t_FP>, 2> & horizontalCoords,
    const std::array<t_FP, 2> & xy0,
    const t_FP depth,
    const t_FP strike,
    const t_FP dipRad,
    const t_FP DV,
    const t_FP nu)
{
    const auto & x = horizontalCoords[0];
    const auto & y = horizontalCoords[1];
    assert(x.size() == y.size());

    const auto numCoords = static_cast<Eigen::Index>(x.size());

    MatrixX2 XY;
    XY.resize(numCoords, Eigen::NoChange);
    // Eigen defaults to column-major ordering, thus the entire first column is stored first, etc.
    std::copy(x.begin(), x.end(), XY.col(0).data());
    std::copy(y.begin(), y.end(), XY.col(1).data());

    // Eigen: Coefficient-wise operations can be applied only to arrays not to matrices.
    // .array() represents the matrix as array and has (usually?) no runtime cost.
    XY.col(0).array() -= xy0[0];
    XY.col(1).array() -= xy0[1];

    const t_FP beta = (strike - 90.f) * pi / 180.f;
    Matrix2 Rz;
    Rz <<
        std::cos(beta), -std::sin(beta),
        std::sin(beta), std::cos(beta);
    const Matrix2X r_beta = Rz * XY.transpose();

    // Create a few array wrappers that are required multiple times
    const auto aX = r_beta.transpose().col(0).array();
    const auto aY = r_beta.transpose().col(1).array();

    // Compute intermediate results that are required multiple times
    const ArrayX1 aXSq = aX.square();
    const ArrayX1 aYSq = aY.square();

    const t_FP d = depth;
    const ArrayX1 r = (aXSq + aYSq + d * d).sqrt();
    const ArrayX1 q = aY * std::sin(dipRad) - d * std::cos(dipRad);

    const t_FP nuScaled = (1.f - 2.f * nu);

    const ArrayX1 rCb = r.cube();
    const ArrayX1 rd = r + d;
    const ArrayX1 rdSq = rd.square();
    const ArrayX1 rdCb = rd.cube();

    const ArrayX1 I1 = nuScaled * aY * (1 / r / rdSq - aXSq * (3 * r + d) / rCb / rdCb);
    const ArrayX1 I2 = nuScaled * aX * (1 / r / rdSq - aYSq * (3 * r + d) / rCb / rdCb);
    const ArrayX1 I3 = nuScaled * aX / rCb - I2;
    const ArrayX1 I5 = nuScaled * (1 / r / rd - aXSq * (2 * r + d) / rCb / rdSq);

    const t_FP sinDipSq = std::sin(dipRad) * std::sin(dipRad);
    const ArrayX1 qSqTimes3DivRPow5 = 3 * q.square() / r.pow(5);
    // Note: For a PTD M0 = DV*mu!
    ArrayX2 ue_un_temp;
    ArrayX3 ue_un_uv;
    ue_un_temp.resize(numCoords, Eigen::NoChange);
    ue_un_uv.resize(numCoords, Eigen::NoChange);

    ue_un_temp.col(0) = DV / 2 / pi * (aX * qSqTimes3DivRPow5 - I3 * sinDipSq);
    ue_un_temp.col(1) = DV / 2 / pi * (aY * qSqTimes3DivRPow5 - I1 * sinDipSq);
    ue_un_uv.col(2) = DV / 2 / pi * (d * qSqTimes3DivRPow5 - I5 * sinDipSq);

    ue_un_uv.block<Eigen::Dynamic, 2>(0, 0, numCoords, 2) =
        (Rz.transpose() * ue_un_temp.matrix().transpose()).transpose();

    return ue_un_uv;
}

}


PCDMBackend::PCDMBackend()
    : QObject()
    , m_state{ State::uninitialized }
{
}

PCDMBackend::~PCDMBackend() = default;

auto PCDMBackend::state() const -> State
{
    return m_state;
}

void PCDMBackend::setHorizontalCoords(const std::array<std::vector<t_FP>, 2> & coords)
{
    if (coords[0].size() != coords[1].size())
    {
        qDebug() << "Input X, Y must have same size";
        setState(State::invalidParameters);
        return;
    }

    m_horizontalCoords = coords;

    setState(State::parametersChanged);
}

const std::array<std::vector<t_FP>, 2> & PCDMBackend::horizontalCoords() const
{
    return m_horizontalCoords;
}

void PCDMBackend::setParameters(const Parameters & parameters)
{
    if (m_parameters == parameters)
    {
        return;
    }

    m_parameters = parameters;

    QString msg;
    if (!parameters.sourceParameters.isValid(&msg))
    {
        qDebug() << msg;
        setState(State::invalidParameters);
        return;
    }

    setState(State::parametersChanged);
}

const PCDMBackend::Parameters & PCDMBackend::parameters() const
{
    return m_parameters;
}

auto PCDMBackend::run() -> State
{
    switch (m_state)
    {
    case PCDMBackend::State::uninitialized:
        break;
    case PCDMBackend::State::parametersChanged:
        break;
    case PCDMBackend::State::invalidParameters:
        qDebug() << "Invalid parameters.";
        return m_state;
    case PCDMBackend::State::resultsReady:
        return m_state; // Nothing to do
    }

    if (m_horizontalCoords[0].size() != m_horizontalCoords[1].size())
    {
        qDebug() << "Input X, Y must have same size";
        return setState(State::invalidParameters);
    }

    if (m_horizontalCoords[0].empty())
    {
        qDebug() << "No input set.";
        return setState(State::invalidParameters);
    }

    const auto inputSize = static_cast<Eigen::Index>(m_horizontalCoords[0].size());

    const auto & omega = m_parameters.sourceParameters.omega;
    const auto & DV = m_parameters.sourceParameters.dv;

    const Vector3 rotationRad = Vector3(omega[0], omega[1], omega[2]) * pi / 180.0f;

    Matrix3 Rx, Ry, Rz;
    Rx = Eigen::AngleAxis<t_FP>(-rotationRad(0), Vector3::UnitX());
    Ry = Eigen::AngleAxis<t_FP>(-rotationRad(1), Vector3::UnitY());
    Rz = Eigen::AngleAxis<t_FP>(-rotationRad(2), Vector3::UnitZ());
    const Matrix3 R = Rz * Ry * Rx;

    const auto Vstrike1 = Vector3(-R(1, 0), R(0, 0), 0.f).normalized();
    auto strike1 = std::atan2(Vstrike1(0), Vstrike1(1)) * 180.f / pi;
    if (std::isnan(strike1))
    {
        strike1 = 0.f;
    }
    const auto dip1Rad = std::acos(R(2, 0));

    const auto Vstrike2 = Vector3(-R(1, 1), R(0, 1), 0.f).normalized();
    auto strike2 = std::atan2(Vstrike2(0), Vstrike2(1)) * 180.f / pi;
    if (std::isnan(strike2))
    {
        strike2 = 0.f;
    }
    const auto dip2Rad = std::acos(R(2, 1));

    const auto Vstrike3 = Vector3(-R(1, 2), R(0, 2), 0.f).normalized();
    auto strike3 = std::atan2(Vstrike3(0), Vstrike3(1)) * 180.f / pi;
    if (std::isnan(strike3))
    {
        strike3 = 0.f;
    }
    const auto dip3Rad = std::acos(R(2, 2));

    // Calculate contribution of the first PTD
    ArrayX3 ue_un_uv1;
    if (DV[0] != 0)
    {
        ue_un_uv1 = PTDdispSurf(
            m_horizontalCoords,
            m_parameters.sourceParameters.horizontalCoord, m_parameters.sourceParameters.depth,
            strike1, dip1Rad, DV[0], m_parameters.nu);
    }
    else
    {
        ue_un_uv1.resize(inputSize, Eigen::NoChange);
        ue_un_uv1.setZero();
    }

    // Calculate contribution of the second PTD
    ArrayX3 ue_un_uv2;
    if (DV[1] != 0)
    {
        ue_un_uv2 = PTDdispSurf(
            m_horizontalCoords,
            m_parameters.sourceParameters.horizontalCoord, m_parameters.sourceParameters.depth,
            strike2, dip2Rad, DV[1], m_parameters.nu);
    }
    else
    {
        ue_un_uv2.resize(inputSize, Eigen::NoChange);
        ue_un_uv2.setZero();
    }

    // Calculate contribution of the third PTD
    ArrayX3 ue_un_uv3;
    if (DV[2] != 0)
    {
        ue_un_uv3 = PTDdispSurf(
            m_horizontalCoords,
            m_parameters.sourceParameters.horizontalCoord, m_parameters.sourceParameters.depth,
            strike3, dip3Rad, DV[2], m_parameters.nu);
    }
    else
    {
        ue_un_uv3.resize(inputSize, Eigen::NoChange);
        ue_un_uv3.setZero();
    }

    const auto numTuples = static_cast<size_t>(ue_un_uv1.rows());
    m_results[0].resize(numTuples);
    m_results[1].resize(numTuples);
    m_results[2].resize(numTuples);

    Eigen::Map<VectorX> ue(m_results[0].data(), ue_un_uv1.rows());
    Eigen::Map<VectorX> un(m_results[1].data(), ue_un_uv1.rows());
    Eigen::Map<VectorX> uv(m_results[2].data(), ue_un_uv1.rows());

    ue = ue_un_uv1.col(0) + ue_un_uv2.col(0) + ue_un_uv3.col(0);
    un = ue_un_uv1.col(1) + ue_un_uv2.col(1) + ue_un_uv3.col(1);
    uv = ue_un_uv1.col(2) + ue_un_uv2.col(2) + ue_un_uv3.col(2);

    return setState(State::resultsReady);
}

const std::array<std::vector<t_FP>, 3> & PCDMBackend::results() const
{
    assert(m_state == State::resultsReady);
    return m_results;
}

std::array<std::vector<t_FP>, 3> && PCDMBackend::takeResults()
{
    assert(m_state == State::resultsReady);
    if (m_state == State::resultsReady)
    {
        m_state = State::parametersChanged;
    }
    return std::move(m_results);
}

auto PCDMBackend::setState(State state) -> State
{
    if (state != State::resultsReady)
    {
        for (auto && vec : m_results)
        {
            vec.clear();
        }
    }

    const auto previousState = m_state;
    m_state = state;
    if (previousState != m_state)
    {
        emit stateChanged(m_state);
    }

    return m_state;
}

bool PCDMBackend::Parameters::operator==(const Parameters & other) const
{
    return sourceParameters == other.sourceParameters
        && nu == other.nu;
}

bool PCDMBackend::Parameters::operator!=(const Parameters & other) const
{
    return !(*this == other);
}
