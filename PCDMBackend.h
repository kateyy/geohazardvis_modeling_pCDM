#pragma once

#include <array>
#include <vector>

#include <QObject>


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
    using t_FP = double;
    
    PCDMBackend();
    ~PCDMBackend() override;

    void setInputs(const std::vector<t_FP> & x, const std::vector<t_FP> & y);

    const std::array<std::vector<t_FP>, 3> & results() const;

    struct PCDMSourceParameters
    {
        std::array<t_FP, 2> horizontalCoord; /// East, north; same coordinate system as input x, y
        t_FP depth;                 /// Positive value
        std::array<t_FP, 3> omega;  /// Clockwise rotation about x, y, z axes in degrees
        std::array<t_FP, 3> dv;     /// Potencies
        t_FP nu;                    /// Poisson's ratio

    };

    void setPCDMParameters(const PCDMSourceParameters & parameters);
    const PCDMSourceParameters & pCDMParameters() const;

    void runModel();

private:
    PCDMSourceParameters m_pCDMParameters;
    std::vector<t_FP> m_x;
    std::vector<t_FP> m_y;

    std::array<std::vector<t_FP>, 3> m_results;
};
