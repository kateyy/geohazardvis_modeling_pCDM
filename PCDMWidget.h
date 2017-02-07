#pragma once

#include <QDockWidget>

#include <memory>


class Ui_PCDMWidget;


class PCDMWidget : public QDockWidget
{
public:
    explicit PCDMWidget(QWidget * parent = nullptr, Qt::WindowFlags flags = {});
    ~PCDMWidget() override;

private:
    std::unique_ptr<Ui_PCDMWidget> m_ui;
};
