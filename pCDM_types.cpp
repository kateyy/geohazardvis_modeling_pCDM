#include "pCDM_types.h"

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
    return horizontalCoord == other.horizontalCoord
        && depth == other.depth
        && omega == other.omega
        && dv == other.dv;
}

bool PointCDMParameters::operator!=(const PointCDMParameters & other) const
{
    return !(*this == other);
}

}
