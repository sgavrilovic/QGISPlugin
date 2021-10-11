/***************************************************************************
  qgswcsprovidergui.cpp
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

#include "qgswcsprovider.h"

#include "qgswcssourceselect.h"
#include "qgssourceselectprovider.h"
#include "qgsproviderguimetadata.h"
#include "qgswcsdataitemguiprovider.h"


//! Provider for WCS layers source select
class QgsWcsSourceSelectProvider : public QgsSourceSelectProvider
{
  public:

    QString providerKey() const override { return QStringLiteral( "wcs" ); }
    QString text() const override { return QObject::tr( "WCS" ); }
    int ordering() const override { return QgsSourceSelectProvider::OrderRemoteProvider + 30; }
    QIcon icon() const override { return QgsApplication::getThemeIcon( QStringLiteral( "/mActionAddWcsLayer.svg" ) ); }
    QgsAbstractDataSourceWidget *createDataSourceWidget( QWidget *parent = nullptr, Qt::WindowFlags fl = Qt::Widget, QgsProviderRegistry::WidgetMode widgetMode = QgsProviderRegistry::WidgetMode::Embedded ) const override
    {
      return new QgsWCSSourceSelect( parent, fl, widgetMode );
    }
};


class QgsWcsProviderGuiMetadata: public QgsProviderGuiMetadata
{
  public:
    QgsWcsProviderGuiMetadata()
      : QgsProviderGuiMetadata( QgsWcsProvider::WCS_KEY )
    {
    }

    QList<QgsSourceSelectProvider *> sourceSelectProviders() override
    {
      QList<QgsSourceSelectProvider *> providers;
      providers << new QgsWcsSourceSelectProvider;
      return providers;
    }

    QList<QgsDataItemGuiProvider *> dataItemGuiProviders() override
    {
      return QList<QgsDataItemGuiProvider *>()
             << new QgsWcsDataItemGuiProvider;
    }
};


QGISEXTERN QgsProviderGuiMetadata *providerGuiMetadataFactory()
{
  return new QgsWcsProviderGuiMetadata();
}
