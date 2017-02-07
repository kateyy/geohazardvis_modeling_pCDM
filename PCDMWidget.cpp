#include "PCDMWidget.h"
#include "ui_PCDMWidget.h"


PCDMWidget::PCDMWidget(QWidget * parent, Qt::WindowFlags flags)
    : QDockWidget(parent, flags)
    , m_ui{ std::make_unique<Ui_PCDMWidget>() }
{
    m_ui->setupUi(this);
}

PCDMWidget::~PCDMWidget() = default;
