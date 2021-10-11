/***************************************************************************
    qgsmaptooladdregularpolygon.cpp  -  map tool for adding regular polygon
    ---------------------
    begin                : July 2017
    copyright            : (C) 2017
    email                : lbartoletti at tuxfamily dot org
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsmaptooladdregularpolygon.h"
#include "qgsgeometryrubberband.h"
#include "qgsgeometryutils.h"
#include "qgsmapcanvas.h"
#include "qgspoint.h"
#include "qgisapp.h"
#include "qgsstatusbar.h"
#include "qgssnapindicator.h"

QgsMapToolAddRegularPolygon::QgsMapToolAddRegularPolygon( QgsMapToolCapture *parentTool, QgsMapCanvas *canvas, CaptureMode mode )
  : QgsMapToolAddAbstract( parentTool, canvas, mode )
{
  mToolName = tr( "Add regular polygon" );
}

void QgsMapToolAddRegularPolygon::createNumberSidesSpinBox()
{
  deleteNumberSidesSpinBox();
  mNumberSidesSpinBox = std::make_unique<QgsSpinBox>();
  mNumberSidesSpinBox->setMaximum( 99999999 );
  mNumberSidesSpinBox->setMinimum( 3 );
  mNumberSidesSpinBox->setPrefix( tr( "Number of sides: " ) );
  mNumberSidesSpinBox->setValue( mNumberSides );
  QgisApp::instance()->addUserInputWidget( mNumberSidesSpinBox.get() );
}

void QgsMapToolAddRegularPolygon::deleteNumberSidesSpinBox()
{
  if ( mNumberSidesSpinBox )
  {
    mNumberSidesSpinBox.reset( nullptr );
  }
}

void QgsMapToolAddRegularPolygon::deactivate()
{
  if ( !mParentTool || mRegularPolygon.isEmpty() )
  {
    return;
  }
  mParentTool->clearCurve( );

  // keep z value from the first snapped point
  std::unique_ptr<QgsLineString> ls( mRegularPolygon.toLineString() );
  for ( const QgsPoint &point : std::as_const( mPoints ) )
  {
    if ( QgsWkbTypes::hasZ( point.wkbType() ) &&
         point.z() != defaultZValue() )
    {
      ls->dropZValue();
      ls->addZValue( point.z() );
      break;
    }
  }

  mParentTool->addCurve( ls.release() );
  clean();

  QgsMapToolCapture::deactivate();
}

void QgsMapToolAddRegularPolygon::clean()
{
  QgsMapToolAddAbstract::clean();

  if ( mNumberSidesSpinBox )
  {
    deleteNumberSidesSpinBox();
  }

  mRegularPolygon = QgsRegularPolygon();
}
