#pragma once

#include <QObject>


class PCDMWidget_StateHelper : public QObject
{
    Q_OBJECT

signals:
    void projectLoadedWithValidSurface();
    void projectLoadedWithInvalidSurface();
    void projectUnloaded();

    void validSurfaceSaved();

    void computingModel();
    void computingEnded();
};
