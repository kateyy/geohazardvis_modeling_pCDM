#pragma once

#include <memory>

#include <gui/plugin/GuiPlugin.h>


class PCDMWidget;


class PCDMPlugin : public GuiPlugin
{
public:
    explicit PCDMPlugin(
        const QString & name,
        const QString & description,
        const QString & vendor,
        const QString & version,
        GuiPluginInterface && pluginInterface);

    ~PCDMPlugin() override;

private:
    std::unique_ptr<PCDMWidget> m_widget;
};
