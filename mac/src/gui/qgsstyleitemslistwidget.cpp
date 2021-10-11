/***************************************************************************
 qgsstyleitemslistwidget.cpp
 ---------------------------
 begin                : June 2019
 copyright            : (C) 2019 by Nyall Dawson
 email                : nyall dot dawson at gmail.com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/


#include "qgsstyleitemslistwidget.h"
#include "qgsstylemanagerdialog.h"
#include "qgsstylesavedialog.h"
#include "qgspanelwidget.h"
#include "qgssettings.h"
#include "qgsgui.h"
#include "qgswindowmanagerinterface.h"
#include "qgsapplication.h"

//
// QgsReadOnlyStyleModel
//

///@cond PRIVATE
QgsReadOnlyStyleModel::QgsReadOnlyStyleModel( QgsStyleModel *sourceModel, QObject *parent )
  : QgsStyleProxyModel( sourceModel, parent )
{

}

QgsReadOnlyStyleModel::QgsReadOnlyStyleModel( QgsStyle *style, QObject *parent )
  : QgsStyleProxyModel( style, parent )
{

}

Qt::ItemFlags QgsReadOnlyStyleModel::flags( const QModelIndex &index ) const
{
  return QgsStyleProxyModel::flags( index ) & ~Qt::ItemIsEditable;
}

QVariant QgsReadOnlyStyleModel::data( const QModelIndex &index, int role ) const
{
  if ( role == Qt::FontRole )
  {
    // drop font size to get reasonable amount of item name shown
    QFont f = QgsStyleProxyModel::data( index, role ).value< QFont >();
    f.setPointSize( 9 );
    return f;
  }
  return QgsStyleProxyModel::data( index, role );
}

///@endcond


//
// QgsStyleItemsListWidget
//

QgsStyleItemsListWidget::QgsStyleItemsListWidget( QWidget *parent )
  : QWidget( parent )
{
  setupUi( this );

  btnAdvanced->hide(); // advanced button is hidden by default
  btnAdvanced->setMenu( new QMenu( this ) );

  const double iconSize = Qgis::UI_SCALE_FACTOR * fontMetrics().horizontalAdvance( 'X' ) * 10;
  viewSymbols->setIconSize( QSize( static_cast< int >( iconSize ), static_cast< int >( iconSize * 0.9 ) ) );  // ~100, 90 on low dpi

  const double treeIconSize = Qgis::UI_SCALE_FACTOR * fontMetrics().horizontalAdvance( 'X' ) * 2;
  mSymbolTreeView->setIconSize( QSize( static_cast< int >( treeIconSize ), static_cast< int >( treeIconSize ) ) );
  mSymbolTreeView->setMinimumHeight( mSymbolTreeView->fontMetrics().height() * 6 );

  viewSymbols->setSelectionBehavior( QAbstractItemView::SelectRows );
  mSymbolTreeView->setSelectionMode( viewSymbols->selectionMode() );

  connect( openStyleManagerButton, &QToolButton::clicked, this, &QgsStyleItemsListWidget::openStyleManager );

  lblSymbolName->clear();

  connect( mButtonIconView, &QToolButton::toggled, this, [ = ]( bool active )
  {
    if ( active )
    {
      mSymbolViewStackedWidget->setCurrentIndex( 0 );
      // note -- we have to save state here and not in destructor, as new symbol list widgets are created before the previous ones are destroyed
      QgsSettings().setValue( QStringLiteral( "UI/symbolsList/lastIconView" ), 0, QgsSettings::Gui );
    }
  } );
  connect( mButtonListView, &QToolButton::toggled, this, [ = ]( bool active )
  {
    if ( active )
    {
      QgsSettings().setValue( QStringLiteral( "UI/symbolsList/lastIconView" ), 1, QgsSettings::Gui );
      mSymbolViewStackedWidget->setCurrentIndex( 1 );
    }
  } );

  // restore previous view
  const QgsSettings settings;
  const int currentView = settings.value( QStringLiteral( "UI/symbolsList/lastIconView" ), 0, QgsSettings::Gui ).toInt();
  if ( currentView == 0 )
    mButtonIconView->setChecked( true );
  else
    mButtonListView->setChecked( true );

  mSymbolTreeView->header()->restoreState( settings.value( QStringLiteral( "UI/symbolsList/treeState" ), QByteArray(), QgsSettings::Gui ).toByteArray() );
  connect( mSymbolTreeView->header(), &QHeaderView::sectionResized, this, [this]
  {
    // note -- we have to save state here and not in destructor, as new symbol list widgets are created before the previous ones are destroyed
    QgsSettings().setValue( QStringLiteral( "UI/symbolsList/treeState" ), mSymbolTreeView->header()->saveState(), QgsSettings::Gui );
  } );

  QgsFilterLineEdit *groupEdit = new QgsFilterLineEdit();
  groupEdit->setShowSearchIcon( true );
  groupEdit->setShowClearButton( true );
  groupEdit->setPlaceholderText( tr( "Filter symbols…" ) );
  groupsCombo->setLineEdit( groupEdit );

  connect( btnSaveSymbol, &QPushButton::clicked, this, &QgsStyleItemsListWidget::saveEntity );
}

void QgsStyleItemsListWidget::setStyle( QgsStyle *style )
{
  mStyle = style;

  mModel = mStyle == QgsStyle::defaultStyle() ? new QgsReadOnlyStyleModel( QgsApplication::defaultStyleModel(), this )
           : new QgsReadOnlyStyleModel( mStyle, this );

  mModel->addDesiredIconSize( viewSymbols->iconSize() );
  mModel->addDesiredIconSize( mSymbolTreeView->iconSize() );
  viewSymbols->setModel( mModel );
  mSymbolTreeView->setModel( mModel );

  connect( mStyle, &QgsStyle::groupsModified, this, &QgsStyleItemsListWidget::populateGroups );

  mSymbolTreeView->setSelectionModel( viewSymbols->selectionModel() );
  connect( viewSymbols->selectionModel(), &QItemSelectionModel::currentChanged, this, &QgsStyleItemsListWidget::onSelectionChanged );

  populateGroups();
  connect( groupsCombo, static_cast<void ( QComboBox::* )( int )>( &QComboBox::currentIndexChanged ), this, &QgsStyleItemsListWidget::groupsCombo_currentIndexChanged );
  connect( groupsCombo, &QComboBox::currentTextChanged, this, &QgsStyleItemsListWidget::updateModelFilters );

  const QgsSettings settings;
  mSymbolTreeView->header()->restoreState( settings.value( QStringLiteral( "UI/symbolsList/treeState" ), QByteArray(), QgsSettings::Gui ).toByteArray() );
}

void QgsStyleItemsListWidget::setEntityType( QgsStyle::StyleEntity type )
{
  mModel->setEntityFilterEnabled( true );
  mModel->setEntityFilter( type );
  const int allGroup = groupsCombo->findData( QVariant( "all" ) );
  switch ( type )
  {
    case QgsStyle::SymbolEntity:
      btnSaveSymbol->setText( tr( "Save Symbol…" ) );
      btnSaveSymbol->setToolTip( tr( "Save symbol to styles" ) );
      if ( allGroup >= 0 )
        groupsCombo->setItemText( allGroup, tr( "All Symbols" ) );
      break;

    case QgsStyle::ColorrampEntity:
      btnSaveSymbol->setText( tr( "Save Color Ramp…" ) );
      btnSaveSymbol->setToolTip( tr( "Save color ramp to styles" ) );
      if ( allGroup >= 0 )
        groupsCombo->setItemText( allGroup, tr( "All Color Ramps" ) );
      break;

    case QgsStyle::TextFormatEntity:
      btnSaveSymbol->setText( tr( "Save Format…" ) );
      btnSaveSymbol->setToolTip( tr( "Save text format to styles" ) );
      if ( allGroup >= 0 )
        groupsCombo->setItemText( allGroup, tr( "All Text Formats" ) );
      break;

    case QgsStyle::LabelSettingsEntity:
      btnSaveSymbol->setText( tr( "Save Label Settings…" ) );
      btnSaveSymbol->setToolTip( tr( "Save label settings to styles" ) );
      if ( allGroup >= 0 )
        groupsCombo->setItemText( allGroup, tr( "All Label Settings" ) );
      break;

    case QgsStyle::LegendPatchShapeEntity:
      btnSaveSymbol->setText( tr( "Save Legend Patch Shape…" ) );
      btnSaveSymbol->setToolTip( tr( "Save legend patch shape to styles" ) );
      if ( allGroup >= 0 )
        groupsCombo->setItemText( allGroup, tr( "All Legend Patch Shapes" ) );
      break;

    case QgsStyle::Symbol3DEntity:
      btnSaveSymbol->setText( tr( "Save 3D Symbol…" ) );
      btnSaveSymbol->setToolTip( tr( "Save 3D symbol to styles" ) );
      if ( allGroup >= 0 )
        groupsCombo->setItemText( allGroup, tr( "All 3D Symbols" ) );
      break;

    case QgsStyle::TagEntity:
    case QgsStyle::SmartgroupEntity:
      break;
  }
}

void QgsStyleItemsListWidget::setEntityTypes( const QList<QgsStyle::StyleEntity> &filters )
{
  mModel->setEntityFilterEnabled( true );
  mModel->setEntityFilters( filters );

  // bit of a gross hack -- run now! this will need revisiting when other parent widgets use different filter combinations!
  const int allGroup = groupsCombo->findData( QVariant( "all" ) );
  if ( filters.length() == 2 && filters.contains( QgsStyle::LabelSettingsEntity ) && filters.contains( QgsStyle::TextFormatEntity ) )
  {
    btnSaveSymbol->setText( tr( "Save Settings…" ) );
    btnSaveSymbol->setToolTip( tr( "Save label settings or text format to styles" ) );
    if ( allGroup >= 0 )
      groupsCombo->setItemText( allGroup, tr( "All Settings" ) );
  }
}

void QgsStyleItemsListWidget::setSymbolType( Qgis::SymbolType type )
{
  mModel->setSymbolTypeFilterEnabled( true );
  mModel->setSymbolType( type );
}

void QgsStyleItemsListWidget::setLayerType( QgsWkbTypes::GeometryType type )
{
  mModel->setLayerType( type );
}

QString QgsStyleItemsListWidget::currentTagFilter() const
{
  return groupsCombo->currentData().toString() == QLatin1String( "tag" ) ? groupsCombo->currentText() : QString();
}

QMenu *QgsStyleItemsListWidget::advancedMenu()
{
  return btnAdvanced->menu();
}

void QgsStyleItemsListWidget::setAdvancedMenu( QMenu *menu )
{
  if ( menu ) // show it if there is a menu pointer
  {
    btnAdvanced->show();
    btnAdvanced->setMenu( menu );
  }
}

void QgsStyleItemsListWidget::showAdvancedButton( bool enabled )
{
  btnAdvanced->setVisible( enabled );
}

QString QgsStyleItemsListWidget::currentItemName() const
{
  const QItemSelection selection = viewSymbols->selectionModel()->selection();
  if ( selection.isEmpty() )
    return QString();

  const QModelIndex index = selection.at( 0 ).topLeft();

  return mModel->data( index, QgsStyleModel::Name ).toString();
}

QgsStyle::StyleEntity QgsStyleItemsListWidget::currentEntityType() const
{
  const QItemSelection selection = viewSymbols->selectionModel()->selection();
  if ( selection.isEmpty() )
    return QgsStyle::SymbolEntity;

  const QModelIndex index = selection.at( 0 ).topLeft();

  return static_cast< QgsStyle::StyleEntity >( mModel->data( index, QgsStyleModel::TypeRole ).toInt() );
}

void QgsStyleItemsListWidget::showEvent( QShowEvent *event )
{
  // restore header sizes on show event -- because this widget is used in multiple places simultaneously
  // (e.g. layer styling dock, it's shown in both the symbology and labeling sections), then we want
  // to ensure that a header resize for any of the widgets applies the next time any other item list widgets
  // are shown.
  QWidget::showEvent( event );
  const QgsSettings settings;
  mSymbolTreeView->header()->restoreState( settings.value( QStringLiteral( "UI/symbolsList/treeState" ), QByteArray(), QgsSettings::Gui ).toByteArray() );
}

void QgsStyleItemsListWidget::populateGroups()
{
  if ( !mStyle )
    return;

  mUpdatingGroups = true;
  groupsCombo->blockSignals( true );
  groupsCombo->clear();

  groupsCombo->addItem( tr( "Favorites" ), QVariant( "favorite" ) );

  QString allText = tr( "All Symbols" );
  if ( mModel->entityFilterEnabled() )
  {
    switch ( mModel->entityFilter() )
    {
      case QgsStyle::SymbolEntity:
        allText = tr( "All Symbols" );
        break;

      case QgsStyle::ColorrampEntity:
        allText = tr( "All Color Ramps" );
        break;

      case QgsStyle::TextFormatEntity:
        allText = tr( "All Text Formats" );
        break;

      case QgsStyle::LabelSettingsEntity:
        allText = tr( "All Label Settings" );
        break;

      case QgsStyle::LegendPatchShapeEntity:
        allText = tr( "All Legend Patch Shapes" );
        break;

      case QgsStyle::Symbol3DEntity:
        allText = tr( "All 3D Symbols" );
        break;

      case QgsStyle::TagEntity:
      case QgsStyle::SmartgroupEntity:
        break;
    }
  }

  groupsCombo->addItem( allText, QVariant( "all" ) );

  int index = 2;
  QStringList tags = mStyle->tags();
  if ( tags.count() > 0 )
  {
    tags.sort();
    groupsCombo->insertSeparator( index );
    const auto constTags = tags;
    for ( const QString &tag : constTags )
    {
      groupsCombo->addItem( tag, QVariant( "tag" ) );
      index++;
    }
  }

  QStringList groups = mStyle->smartgroupNames();
  if ( groups.count() > 0 )
  {
    groups.sort();
    groupsCombo->insertSeparator( index + 1 );
    const auto constGroups = groups;
    for ( const QString &group : constGroups )
    {
      groupsCombo->addItem( group, QVariant( "smartgroup" ) );
    }
  }
  groupsCombo->blockSignals( false );

  const QgsSettings settings;
  index = settings.value( QStringLiteral( "qgis/symbolsListGroupsIndex" ), 0 ).toInt();
  groupsCombo->setCurrentIndex( index );

  mUpdatingGroups = false;

  updateModelFilters();
}

void QgsStyleItemsListWidget::updateModelFilters()
{
  if ( mUpdatingGroups || !mModel )
    return;

  const QString text = groupsCombo->currentText();
  const bool isFreeText = text != groupsCombo->itemText( groupsCombo->currentIndex() );

  if ( isFreeText )
  {
    mModel->setFavoritesOnly( false );
    mModel->setTagId( -1 );
    mModel->setSmartGroupId( -1 );
    mModel->setFilterString( groupsCombo->currentText() );
  }
  else if ( groupsCombo->currentData().toString() == QLatin1String( "favorite" ) )
  {
    mModel->setFavoritesOnly( true );
    mModel->setTagId( -1 );
    mModel->setSmartGroupId( -1 );
    mModel->setFilterString( QString() );
  }
  else if ( groupsCombo->currentData().toString() == QLatin1String( "all" ) )
  {
    mModel->setFavoritesOnly( false );
    mModel->setTagId( -1 );
    mModel->setSmartGroupId( -1 );
    mModel->setFilterString( QString() );
  }
  else if ( groupsCombo->currentData().toString() == QLatin1String( "smartgroup" ) )
  {
    mModel->setFavoritesOnly( false );
    mModel->setTagId( -1 );
    mModel->setSmartGroupId( mStyle->smartgroupId( text ) );
    mModel->setFilterString( QString() );
  }
  else
  {
    mModel->setFavoritesOnly( false );
    mModel->setTagId( mStyle->tagId( text ) );
    mModel->setSmartGroupId( -1 );
    mModel->setFilterString( QString() );
  }
}

void QgsStyleItemsListWidget::openStyleManager()
{
  // prefer to use global window manager to open the style manager, if possible!
  // this allows reuse of an existing non-modal window instead of opening a new modal window.
  // Note that we only use the non-modal dialog if we're open in the panel -- if we're already
  // open as part of a modal dialog, then we MUST use another modal dialog or the result will
  // not be focusable!
  QgsPanelWidget *panel = QgsPanelWidget::findParentPanel( this );
  if ( !panel || !panel->dockMode()
       || !QgsGui::windowManager()
       || !QgsGui::windowManager()->openStandardDialog( QgsWindowManagerInterface::DialogStyleManager ) )
  {
    // fallback to modal dialog
    QgsStyleManagerDialog dlg( mStyle, this );
    dlg.exec();

    updateModelFilters(); // probably not needed -- the model should automatically update if any changes were made
  }
}

void QgsStyleItemsListWidget::onSelectionChanged( const QModelIndex &index )
{
  if ( !mModel )
    return;

  const QString symbolName = mModel->data( mModel->index( index.row(), QgsStyleModel::Name ) ).toString();
  lblSymbolName->setText( symbolName );

  emit selectionChanged( symbolName, static_cast< QgsStyle::StyleEntity >( mModel->data( index, QgsStyleModel::TypeRole ).toInt() ) );
}

void QgsStyleItemsListWidget::groupsCombo_currentIndexChanged( int index )
{
  QgsSettings settings;
  settings.setValue( QStringLiteral( "qgis/symbolsListGroupsIndex" ), index );
}
