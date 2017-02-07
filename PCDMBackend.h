#pragma once

#include <QObject>


class PCDMBackend : public QObject
{
    Q_OBJECT

public:
    PCDMBackend();
    ~PCDMBackend() override;
};
