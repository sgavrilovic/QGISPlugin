/***************************************************************************
  qgsspatialiteprovidergui.cpp
  --------------------------------------
  Date                 : June 2019
  Copyright            : (C) 2019 by Martin Dobias
  Email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsapplication.h"
#include "qgsproviderguimetadata.h"
#include "qgssourceselectprovider.h"

#include "qgsspatialitedataitemguiprovider.h"
#include "qgsspatialitesourceselect.h"
#include "qgsspatialiteprovider.h"


//! Provider for spatialite source select
class QgsSpatialiteSourceSelectProvider : public QgsSourceSelectProvider
{
  public:

    QString providerKey() const override { return QStringLiteral( "spatialite" ); }
    QString text() const override { return QObject::tr( "SpatiaLite" ); }
    int ordering() const override { return QgsSourceSelectProvider::OrderDatabaseProvider + 10; }
    QIcon icon() const override { return QgsApplication::getThemeIcon( QStringLiteral( "/mActionAddSpatiaLiteLayer.svg" ) ); }
    QgsAbstractDataSourceWidget *createDataSourceWidget( QWidget *parent = nullptr, Qt::WindowFlags fl = Qt::Widget, QgsProviderRegistry::WidgetMode widgetMode = QgsProviderRegistry::WidgetMode::Embedded ) const override
    {
      return new QgsSpatiaLiteSourceSelect( parent, fl, widgetMode );
    }
};


class QgsSpatiaLiteProviderGuiMetadata: public QgsProviderGuiMetadata
{
  public:
    QgsSpatiaLiteProviderGuiMetadata()
      : QgsProviderGuiMetadata( QgsSpatiaLiteProvider::SPATIALITE_KEY )
    {
    }

    QList<QgsSourceSelectProvider *> sourceSelectProviders() override
    {
      QList<QgsSourceSelectProvider *> providers;
      providers << new QgsSpatialiteSourceSelectProvider;
      return providers;
    }

    QList<QgsDataItemGuiProvider *> dataItemGuiProviders() override
    {
      return QList<QgsDataItemGuiProvider *>()
             << new QgsSpatiaLiteDataItemGuiProvider;
    }
};


QGISEXTERN QgsProviderGuiMetadata *providerGuiMetadataFactory()
{
  return new QgsSpatiaLiteProviderGuiMetadata();
}
