#pragma once

#include <array>

class QString;


namespace pCDM
{

using t_FP = double;


struct PointCDMParameters
{
    /** Position: easting, northing; same coordinate system as and unit as the input x, y coordinates */
    std::array<t_FP, 2> horizontalCoord;
    /** Positive value for the depth of the point source. */
    t_FP depth;
    /** Clockwise rotation about x, y, z axes in degrees */
    std::array<t_FP, 3> omega;
    /**
     * Potencies of the PTDs that before applying the rotations are normal to the X, Y and Z axes,
     * respectively. The potency has the unit of volume (the unit of displacements and CDM
     * semi-axes to the power of 3).
     */
    std::array<t_FP, 3> dv;

    /**
     * Check if supplied parameters are valid. If not and errorMessage points to a valid string,
     * a user-friendly error message is provided.
     */
    bool isValid(QString * errorMessage = nullptr) const;

    bool operator==(const PointCDMParameters & other) const;
    bool operator!=(const PointCDMParameters & other) const;
};

}
