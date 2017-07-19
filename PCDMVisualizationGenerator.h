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

#pragma once

#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>


class AbstractRenderView;
class DataMapping;
class DataObject;
class PCDMModel;
class PCDMProject;
class ResidualVerificationView;


class PCDMVisualizationGenerator : public QObject
{
    Q_OBJECT

public:
    explicit PCDMVisualizationGenerator(DataMapping & dataMapping);
    ~PCDMVisualizationGenerator() override;

    void setProject(PCDMProject * project);
    PCDMProject * project();

    /**
     * Open a render view that can later be used to visualize modeling results.
     * This class will close the render view only in the destructor. This allows users to use the
     * same view/GUI setup while switching between projects and models.
     */
    void openRenderView();
    /**
     * Same as openRenderView(), but opens a Residual verification view instead.
     * This view can be used next to the normal render view, but the views will always visualize
     * the same modeling data.
     */
    void openResidualView();
    /**
     * Create a data object for the horizontal coordinates of the current project.
     * This data object gets deleted when the project coordinates are changed.
     * Point data attribute arrays are initialized. Call showModel() to fill them with modeling
     * result values.
     */
    DataObject * dataObject();
    /**
     * Show the data object in the preview renderer, without visualizing modeling results.
     * This does not modify the current color mapping, it only ensures that the render view and
     * data object are visible.
     */
    void showDataObject();
    void showDataObjectsInResidualView();
    /**
     * Update the attribute arrays of dataObject() for the selected model.
     * If a render view is opened, this function will add the data object to the view and configure
     * color mappings to visualize "uv" values.
     */
    void setModel(PCDMModel & model);
    /**
     * Utility function to open a render view, update the preview data to represent the specified
     * model and visualize it.
     */
    void showModel(PCDMModel & model);
    /*
     * Same as showModel(), but uses the residual view created by openResidualView().
     */
    void showResidualForModel(PCDMModel & model);

    void cleanup();

private:
    void updateForNewCoordinates();
    void configureVisualizations(bool validResults) const;

private:
    DataMapping & m_dataMapping;
    QPointer<PCDMProject> m_project;

    std::vector<QMetaObject::Connection> m_projectConnections;

    std::unique_ptr<DataObject> m_dataObject;
    QPointer<AbstractRenderView> m_renderView;
    QPointer<ResidualVerificationView> m_residualView;
};
