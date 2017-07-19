/*
 * GeohazardVis plug-in: pCDM Modeling
 * Copyright (C) 2017 Karsten Tausche <geodev@posteo.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PCDMPlugin.h"

#include <utility>

#include <gui/plugin/GuiPluginInterface.h>

#include "PCDMWidget.h"


PCDMPlugin::PCDMPlugin(
    const QString & name,
    const QString & description,
    const QString & vendor,
    const QString & version,
    GuiPluginInterface && pluginInterface)
    : GuiPlugin(name, description, vendor, version, std::forward<GuiPluginInterface>(pluginInterface))
    , m_widget{ std::make_unique<PCDMWidget>("Modeling_pCDM", m_pluginInterface) }
{
    m_pluginInterface.addWidget(m_widget.get());
}

PCDMPlugin::~PCDMPlugin()
{
    m_pluginInterface.removeWidget(m_widget.get());
}
