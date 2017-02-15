#include "PCDMPlugin.h"

#include <gui/plugin/GuiPluginInterface.h>

#include "PCDMWidget.h"


PCDMPlugin::PCDMPlugin(
    const QString & name,
    const QString & description,
    const QString & vendor,
    const QString & version,
    GuiPluginInterface && pluginInterface)
    : GuiPlugin(name, description, vendor, version, std::move(pluginInterface))
    , m_widget{ std::make_unique<PCDMWidget>("Modeling_pCDM", m_pluginInterface) }
{
    m_pluginInterface.addWidget(m_widget.get());
}

PCDMPlugin::~PCDMPlugin()
{
    m_pluginInterface.removeWidget(m_widget.get());
}
