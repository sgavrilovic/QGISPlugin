#include "SimCenterMapcanvasWidget.h"
#include "QGISVisualizationWidget.h"

#include <qgsmapcanvas.h>
#include <qgsvectorlayer.h>
#include <qgsappmaptools.h>
#include <qgsmaptool.h>
#include <qgslayertreeview.h>
#include <qgslayertree.h>
#include <qgslayertreemodel.h>
#include <qgslayertreelayer.h>
#include <qgisapp.h>
#include <qgsadvanceddigitizingdockwidget.h>
#include <qgsapplayertreeviewmenuprovider.h>
#include <qgsmaptoolcapture.h>
#include <qgsmaptoolselect.h>
#include <qgsmaptoolselectionhandler.h>

#include <QVBoxLayout>
#include <QToolButton>
#include <QSplitter>

SimCenterMapcanvasWidget::SimCenterMapcanvasWidget(const QString &name, QGISVisualizationWidget *mainVisWidget) : theVisualizationWidget(mainVisWidget)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setMargin(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    this->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setWindowTitle(name);
    thisMapCanvas = new QgsMapCanvas();

    mainCanvas = theVisualizationWidget->getMainCanvas();

    thisMapCanvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    this->setAcceptDrops(true);
    thisMapCanvas->setAcceptDrops(true);

    auto mainWidget = new QSplitter();
    mainWidget->setContentsMargins(0, 0, 0, 0);
    mainWidget->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);

    mainLayout->addWidget(mainWidget);

    auto layerTreeView = theVisualizationWidget->getLayerTreeView();
    auto model = layerTreeView->layerTreeModel();

    //    rootNode = new QgsLayerTree();
    //    QgsLayerTreeModel *treeModel = new QgsLayerTreeModel(rootNode, this);
    //    treeModel->setFlag( QgsLayerTreeModel::AllowNodeReorder );
    //    treeModel->setFlag( QgsLayerTreeModel::AllowNodeRename );
    //    treeModel->setFlag( QgsLayerTreeModel::ShowLegend );
    //    treeModel->setFlag( QgsLayerTreeModel::AllowNodeChangeVisibility );
    //    treeModel->setFlag( QgsLayerTreeModel::UseTextFormatting );
    //    treeModel->setAutoCollapseLegendNodes( 10 );

    legendTreeView = new QgsLayerTreeView(this);
    // legendTreeView->setStyleSheet("QTreeView::item:hover{background-color:#FFFF00;}");
    legendTreeView->setModel(model);

    auto qGISApp = theVisualizationWidget->getQgis();

    legendTreeView->setMenuProvider( new QgsAppLayerTreeViewMenuProvider(legendTreeView, thisMapCanvas));

    connect( legendTreeView, &QAbstractItemView::clicked, this, &SimCenterMapcanvasWidget::layerTreeViewClicked );
    connect( legendTreeView, &QAbstractItemView::doubleClicked, qGISApp, &QgisApp::layerTreeViewDoubleClicked );
    connect( legendTreeView->selectionModel(), &QItemSelectionModel::currentChanged, qGISApp, &QgisApp::updateNewLayerInsertionPoint );
    connect( legendTreeView->selectionModel(), &QItemSelectionModel::selectionChanged,
             qGISApp, &QgisApp::legendLayerSelectionChanged );

    //connect( legendTreeView, &QgsLayerTreeView::currentLayerChanged, qGISApp, &QgisApp::onActiveLayerChanged );
    //    connect( legendTreeView->selectionModel(), &QItemSelectionModel::selectionChanged,
    //             qGISApp, &QgisApp::activateDeactivateMultipleLayersRelatedActions );
    //    connect( legendTreeView->layerTreeModel()->rootGroup(), &QgsLayerTreeNode::addedChildren,
    //             qGISApp, &QgisApp::markDirty );
    //    connect( legendTreeView->layerTreeModel()->rootGroup(), &QgsLayerTreeNode::addedChildren,
    //             qGISApp, &QgisApp::updateNewLayerInsertionPoint );
    //    connect( legendTreeView->layerTreeModel()->rootGroup(), &QgsLayerTreeNode::removedChildren,
    //             qGISApp, &QgisApp::markDirty );
    //    connect( legendTreeView->layerTreeModel()->rootGroup(), &QgsLayerTreeNode::removedChildren,
    //             qGISApp, &QgisApp::updateNewLayerInsertionPoint );
    //    connect( legendTreeView->layerTreeModel()->rootGroup(), &QgsLayerTreeNode::visibilityChanged,
    //             qGISApp, &QgisApp::markDirty );
    //    connect( legendTreeView->layerTreeModel()->rootGroup(), &QgsLayerTreeNode::customPropertyChanged,
    //             qGISApp, [ = ]( QgsLayerTreeNode *, const QString & key )
    //    {
    //        // only mark dirty for non-view only changes
    //        if ( !QgsLayerTreeView::viewOnlyCustomProperties().contains( key ) )
    //            qGISApp->markDirty();
    //    } );

    mainWidget->addWidget(legendTreeView);
    mainWidget->addWidget(thisMapCanvas);

    auto advancedDigitizingDockWidget = new QgsAdvancedDigitizingDockWidget(thisMapCanvas, this);
    mMapTools = std::make_unique<QgsAppMapTools>(thisMapCanvas, advancedDigitizingDockWidget);
    advancedDigitizingDockWidget->close();
    advancedDigitizingDockWidget->setVisible(false);

    auto captureTools = mMapTools->captureTools();

    for ( QgsMapToolCapture *tool : captureTools )
    {
        if (tool->supportsTechnique(Qgis::CaptureTechnique::CircularString))
            tool->setCurrentCaptureTechnique(Qgis::CaptureTechnique::CircularString);
        if (tool->supportsTechnique(Qgis::CaptureTechnique::Streaming))
            tool->setCurrentCaptureTechnique(Qgis::CaptureTechnique::CircularString);
    }

    auto simpleMapToolSelect = mMapTools->mapTool<QgsMapToolSelect>(QgsAppMapTools::SelectFeatures);
    simpleMapToolSelect->setSelectionMode(QgsMapToolSelectionHandler::SelectSimple);


    mMapTools->mapTool<QgsMapToolSelect>(QgsAppMapTools::SelectPolygon)->setSelectionMode(QgsMapToolSelectionHandler::SelectPolygon);
    mMapTools->mapTool<QgsMapToolSelect>(QgsAppMapTools::SelectFreehand)->setSelectionMode(QgsMapToolSelectionHandler::SelectFreehand);

    auto radiusMapToolSelect = mMapTools->mapTool<QgsMapToolSelect>( QgsAppMapTools::SelectRadius );
    radiusMapToolSelect->setSelectionMode( QgsMapToolSelectionHandler::SelectRadius );

    //connect(this,&SimCenterMapcanvasWidget::handleSelectionFinished,radiusMapToolSelect,&QgsMapToolSelectionHandler::geometryChanged);


    // Now add the splitter handle
    // Note: index 0 handle is always hidden, index 1 is between the two widgets
    QSplitterHandle *handle = mainWidget->handle(1);

    if(handle == nullptr)
    {
        qDebug()<<"Error getting the handle";
        return;
    }

    auto buttonHandle = new QToolButton(handle);
    QVBoxLayout *layout = new QVBoxLayout(handle);
    layout->setSpacing(0);
    layout->setMargin(0);

    mainWidget->setHandleWidth(15);

    buttonHandle->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    buttonHandle->setDown(false);
    buttonHandle->setAutoRaise(false);
    buttonHandle->setCheckable(false);
    buttonHandle->setArrowType(Qt::RightArrow);
    buttonHandle->setStyleSheet("QToolButton{border:0px solid}; QToolButton:pressed {border:0px solid}");
    buttonHandle->setIconSize(buttonHandle->size());
    layout->addWidget(buttonHandle);

    mainWidget->setStretchFactor(1,2);
}


void SimCenterMapcanvasWidget::setMapTool(QgsMapTool *mapTool)
{
    thisMapCanvas->setMapTool(mapTool);
}


QgsMapCanvas *SimCenterMapcanvasWidget::mapCanvas()
{
    return thisMapCanvas;
}


void SimCenterMapcanvasWidget::showLabels(bool show)
{
    Qgis::MapSettingsFlags flags = thisMapCanvas->mapSettings().flags();
    if ( show )
        flags = flags | Qgis::MapSettingsFlag::DrawLabeling;
    else
        flags = flags & ~(int)(Qgis::MapSettingsFlag::DrawLabeling);

    thisMapCanvas->setMapSettingsFlags( flags );
}


void SimCenterMapcanvasWidget::showAnnotations(bool show)
{
    thisMapCanvas->setAnnotationsVisible(show);
}


void SimCenterMapcanvasWidget::clear(void)
{
    currentLayer = nullptr;
    selectedIds.clear();
    deselectedIds.clear();
    //    rootNode->clear();
}


QgsFeatureIds SimCenterMapcanvasWidget::getSelectedIds() const
{
    return selectedIds;
}


QgsMapCanvas *SimCenterMapcanvasWidget::getMainCanvas() const
{
    return mainCanvas;
}


void SimCenterMapcanvasWidget::enableSelectionTool(void)
{
    auto selectTool = mMapTools->mapTool(QgsAppMapTools::SelectFeatures);
    thisMapCanvas->setMapTool(selectTool);
    thisMapCanvas->unsetCursor();
}


void SimCenterMapcanvasWidget::enableIdentifyTool(void)
{
    auto selectTool = mMapTools->mapTool(QgsAppMapTools::Identify);
    thisMapCanvas->setMapTool(selectTool);
}


void SimCenterMapcanvasWidget::enablePolygonSelectionTool(void)
{
    auto selectTool = mMapTools->mapTool(QgsAppMapTools::SelectPolygon);
    thisMapCanvas->setMapTool(selectTool);
}


void SimCenterMapcanvasWidget::enableFreehandSelectionTool(void)
{
    auto selectTool = mMapTools->mapTool(QgsAppMapTools::SelectFreehand);
    thisMapCanvas->setMapTool(selectTool);
}


void SimCenterMapcanvasWidget::enableRadiusSelectionTool(void)
{
    auto selectTool = mMapTools->mapTool(QgsAppMapTools::SelectRadius);
    thisMapCanvas->setMapTool(selectTool,true);
}


void SimCenterMapcanvasWidget::enablePanTool(void)
{
    thisMapCanvas->setMapTool(mMapTools->mapTool(QgsAppMapTools::Pan));
}


void SimCenterMapcanvasWidget::setCurrentLayer(QgsVectorLayer* layer)
{
    if(currentLayer != nullptr)
        disconnect(currentLayer,&QgsVectorLayer::selectionChanged,this, &SimCenterMapcanvasWidget::selectionChanged);

    currentLayer = layer;
    thisMapCanvas->setCurrentLayer(currentLayer);

    if(layer != nullptr)
        connect(currentLayer,&QgsVectorLayer::selectionChanged,this, &SimCenterMapcanvasWidget::selectionChanged);
}


void SimCenterMapcanvasWidget::selectionChanged(const QgsFeatureIds &selected, const QgsFeatureIds &deselected, bool clearAndSelect)
{
    Q_UNUSED(clearAndSelect);

    if(!selected.isEmpty())
    {
        auto isVectorLayer = dynamic_cast<QgsVectorLayer*>(QObject::sender());

        if(isVectorLayer)
        {
            auto featureIt = isVectorLayer->getFeatures(selected);

            if(showPopUpOnSelection)
                theVisualizationWidget->showFeaturePopUp(featureIt);
        }
    }

    selectedIds = selected;
    deselectedIds = deselected;
}


void SimCenterMapcanvasWidget::layerTreeViewClicked(const QModelIndex &index)
{
    Q_UNUSED(index)

    if (legendTreeView)
    {
        if (QgsMapLayer *layer = legendTreeView->currentLayer())
        {
            QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer*>(layer);
            this->setCurrentLayer(vlayer);
        }
    }
}


void SimCenterMapcanvasWidget::setShowPopUpOnSelection(bool value)
{
    showPopUpOnSelection = value;
}


void SimCenterMapcanvasWidget::deselectAllTreeItems(void)
{
    auto selectModel = legendTreeView->selectionModel();
    selectModel->clearSelection();
    selectModel->clearCurrentIndex();
}


void SimCenterMapcanvasWidget::handleSelectionFinished(Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers)


    //qDebug()<<"Yes";
}



