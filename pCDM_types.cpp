#include "pCDM_types.h"

#include <cmath>
#include <limits>

#include <QString>


namespace pCDM
{

bool PointCDMParameters::isValid(QString * errorMessage) const
{
    auto setMsg = [errorMessage] (const QString & msg)
    {
        if (errorMessage)
        {
            *errorMessage = msg;
        }
    };

    if (!(dv[0] >= 0.f && dv[1] >= 0.f && dv[2] >= 0.f)
        && !(dv[0] <= 0.f && dv[1] <= 0.f && dv[2] <= 0.f))
    {
        static const QString msg = "Potencies (DV x, y, z) must have the same sign.";
        setMsg(msg);
        return false;
    }

    if (depth < 0.0f)
    {
        static const QString msg = "Depth must be a positive value.";
        setMsg(msg);
        return false;
    }

    setMsg({});

    return true;
}

bool PointCDMParameters::operator==(const PointCDMParameters & other) const
{
    const auto thisValues = {
        &horizontalCoord[0], &horizontalCoord[1],
        &depth,
        &omega[0], &omega[1], &omega[2],
        &dv[0], &dv[1], &dv[2]
    };
    const auto otherValues = {
        &other.horizontalCoord[0], &other.horizontalCoord[1],
        &other.depth,
        &other.omega[0], &other.omega[1], &other.omega[2],
        &other.dv[0], &other.dv[1], &other.dv[2]
    };

    auto thisIt = thisValues.begin();
    auto otherIt = otherValues.begin();
    for (; thisIt != thisValues.end(); ++thisIt, ++otherIt)
    {
        if (std::abs(**thisIt - **otherIt) > std::numeric_limits<t_FP>::epsilon())
        {
            return false;
        }
    }

    return true;
}

bool PointCDMParameters::operator!=(const PointCDMParameters & other) const
{
    return !(*this == other);
}

}
