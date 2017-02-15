#pragma once

#include <array>
#include <vector>

#include <QObject>

#include "pCDM_types.h"


/**
 * pCDM: point Compound Dislocation Model
 * calculates the surface displacements associated with a point CDM that is 
 * composed of three mutually orthogonal point tensile dislocations in a 
 * half-space.
 *
 * Based on Mehdi Nikkhoo's work and MATLAB script:
 * http://volcanodeformation.com/software.html
 *   Created: 2015.5.22
 *   Last modified: 2016.10.18
 *
*/
class PCDMBackend : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        uninitialized,
        parametersChanged,
        invalidParameters,
        resultsReady
    };

    struct Parameters
    {
        pCDM::PointCDMParameters sourceParameters;
        /** Poisson's ratio */
        pCDM::t_FP nu;

        bool operator==(const Parameters & other) const;
        bool operator!=(const Parameters & other) const;
    };

public:
    PCDMBackend();
    ~PCDMBackend() override;

    State state() const;

    void setHorizontalCoords(const std::array<std::vector<pCDM::t_FP>, 2> & coords);
    const std::array<std::vector<pCDM::t_FP>, 2> & horizontalCoords() const;

    void setParameters(const Parameters & parameters);
    const Parameters & parameters() const;

    State run();

    const std::array<std::vector<pCDM::t_FP>, 3> & results() const;
    /** Take the result memory from the backend, omitting an additional copy step. */
    std::array<std::vector<pCDM::t_FP>, 3> && takeResults();

signals:
    void stateChanged(State state);

private:
    State setState(State state);

private:
    State m_state;

    Parameters m_parameters;
    std::array<std::vector<pCDM::t_FP>, 2> m_horizontalCoords;
    std::array<std::vector<pCDM::t_FP>, 3> m_results;
};
