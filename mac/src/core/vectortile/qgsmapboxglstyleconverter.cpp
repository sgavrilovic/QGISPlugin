/***************************************************************************
  qgsmapboxglstyleconverter.cpp
  --------------------------------------
  Date                 : September 2020
  Copyright            : (C) 2020 by Nyall Dawson
  Email                : nyall dot dawson at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/


/*
 * Ported from original work by Martin Dobias, and extended by the MapTiler team!
 */

#include "qgsmapboxglstyleconverter.h"
#include "qgsvectortilebasicrenderer.h"
#include "qgsvectortilebasiclabeling.h"
#include "qgssymbollayer.h"
#include "qgssymbollayerutils.h"
#include "qgslogger.h"
#include "qgsfillsymbollayer.h"
#include "qgslinesymbollayer.h"
#include "qgsfontutils.h"
#include "qgsjsonutils.h"
#include "qgspainteffect.h"
#include "qgseffectstack.h"
#include "qgsblureffect.h"
#include "qgsmarkersymbollayer.h"
#include "qgstextbackgroundsettings.h"
#include "qgsfillsymbol.h"
#include "qgsmarkersymbol.h"
#include "qgslinesymbol.h"

#include <QBuffer>
#include <QRegularExpression>

QgsMapBoxGlStyleConverter::QgsMapBoxGlStyleConverter()
{
}

QgsMapBoxGlStyleConverter::Result QgsMapBoxGlStyleConverter::convert( const QVariantMap &style, QgsMapBoxGlStyleConversionContext *context )
{
  mError.clear();
  mWarnings.clear();
  if ( style.contains( QStringLiteral( "layers" ) ) )
  {
    parseLayers( style.value( QStringLiteral( "layers" ) ).toList(), context );
  }
  else
  {
    mError = QObject::tr( "Could not find layers list in JSON" );
    return NoLayerList;
  }
  return Success;
}

QgsMapBoxGlStyleConverter::Result QgsMapBoxGlStyleConverter::convert( const QString &style, QgsMapBoxGlStyleConversionContext *context )
{
  return convert( QgsJsonUtils::parseJson( style ).toMap(), context );
}

QgsMapBoxGlStyleConverter::~QgsMapBoxGlStyleConverter() = default;

void QgsMapBoxGlStyleConverter::parseLayers( const QVariantList &layers, QgsMapBoxGlStyleConversionContext *context )
{
  std::unique_ptr< QgsMapBoxGlStyleConversionContext > tmpContext;
  if ( !context )
  {
    tmpContext = std::make_unique< QgsMapBoxGlStyleConversionContext >();
    context = tmpContext.get();
  }

  QList<QgsVectorTileBasicRendererStyle> rendererStyles;
  QList<QgsVectorTileBasicLabelingStyle> labelingStyles;

  for ( const QVariant &layer : layers )
  {
    const QVariantMap jsonLayer = layer.toMap();

    const QString layerType = jsonLayer.value( QStringLiteral( "type" ) ).toString();
    if ( layerType == QLatin1String( "background" ) )
      continue;

    const QString styleId = jsonLayer.value( QStringLiteral( "id" ) ).toString();
    context->setLayerId( styleId );
    const QString layerName = jsonLayer.value( QStringLiteral( "source-layer" ) ).toString();

    const int minZoom = jsonLayer.value( QStringLiteral( "minzoom" ), QStringLiteral( "-1" ) ).toInt();
    const int maxZoom = jsonLayer.value( QStringLiteral( "maxzoom" ), QStringLiteral( "-1" ) ).toInt();

    const bool enabled = jsonLayer.value( QStringLiteral( "visibility" ) ).toString() != QLatin1String( "none" );

    QString filterExpression;
    if ( jsonLayer.contains( QStringLiteral( "filter" ) ) )
    {
      filterExpression = parseExpression( jsonLayer.value( QStringLiteral( "filter" ) ).toList(), *context );
    }

    QgsVectorTileBasicRendererStyle rendererStyle;
    QgsVectorTileBasicLabelingStyle labelingStyle;

    bool hasRendererStyle = false;
    bool hasLabelingStyle = false;
    if ( layerType == QLatin1String( "fill" ) )
    {
      hasRendererStyle = parseFillLayer( jsonLayer, rendererStyle, *context );
    }
    else if ( layerType == QLatin1String( "line" ) )
    {
      hasRendererStyle = parseLineLayer( jsonLayer, rendererStyle, *context );
    }
    else if ( layerType == QLatin1String( "circle" ) )
    {
      hasRendererStyle = parseCircleLayer( jsonLayer, rendererStyle, *context );
    }
    else if ( layerType == QLatin1String( "symbol" ) )
    {
      parseSymbolLayer( jsonLayer, rendererStyle, hasRendererStyle, labelingStyle, hasLabelingStyle, *context );
    }
    else
    {
      mWarnings << QObject::tr( "%1: Skipping unknown layer type %2" ).arg( context->layerId(), layerType );
      QgsDebugMsg( mWarnings.constLast() );
      continue;
    }

    if ( hasRendererStyle )
    {
      rendererStyle.setStyleName( styleId );
      rendererStyle.setLayerName( layerName );
      rendererStyle.setFilterExpression( filterExpression );
      rendererStyle.setMinZoomLevel( minZoom );
      rendererStyle.setMaxZoomLevel( maxZoom );
      rendererStyle.setEnabled( enabled );
      rendererStyles.append( rendererStyle );
    }

    if ( hasLabelingStyle )
    {
      labelingStyle.setStyleName( styleId );
      labelingStyle.setLayerName( layerName );
      labelingStyle.setFilterExpression( filterExpression );
      labelingStyle.setMinZoomLevel( minZoom );
      labelingStyle.setMaxZoomLevel( maxZoom );
      labelingStyle.setEnabled( enabled );
      labelingStyles.append( labelingStyle );
    }

    mWarnings.append( context->warnings() );
    context->clearWarnings();
  }

  mRenderer = std::make_unique< QgsVectorTileBasicRenderer >();
  QgsVectorTileBasicRenderer *renderer = dynamic_cast< QgsVectorTileBasicRenderer *>( mRenderer.get() );
  renderer->setStyles( rendererStyles );

  mLabeling = std::make_unique< QgsVectorTileBasicLabeling >();
  QgsVectorTileBasicLabeling *labeling = dynamic_cast< QgsVectorTileBasicLabeling * >( mLabeling.get() );
  labeling->setStyles( labelingStyles );
}

bool QgsMapBoxGlStyleConverter::parseFillLayer( const QVariantMap &jsonLayer, QgsVectorTileBasicRendererStyle &style, QgsMapBoxGlStyleConversionContext &context )
{
  if ( !jsonLayer.contains( QStringLiteral( "paint" ) ) )
  {
    context.pushWarning( QObject::tr( "%1: Layer has no paint property, skipping" ).arg( jsonLayer.value( QStringLiteral( "id" ) ).toString() ) );
    return false;
  }

  const QVariantMap jsonPaint = jsonLayer.value( QStringLiteral( "paint" ) ).toMap();

  QgsPropertyCollection ddProperties;
  QgsPropertyCollection ddRasterProperties;

  // fill color
  QColor fillColor;
  if ( jsonPaint.contains( QStringLiteral( "fill-color" ) ) )
  {
    const QVariant jsonFillColor = jsonPaint.value( QStringLiteral( "fill-color" ) );
    switch ( jsonFillColor.type() )
    {
      case QVariant::Map:
        ddProperties.setProperty( QgsSymbolLayer::PropertyFillColor, parseInterpolateColorByZoom( jsonFillColor.toMap(), context, &fillColor ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddProperties.setProperty( QgsSymbolLayer::PropertyFillColor, parseValueList( jsonFillColor.toList(), PropertyType::Color, context, 1, 255, &fillColor ) );
        break;

      case QVariant::String:
        fillColor = parseColor( jsonFillColor.toString(), context );
        break;

      default:
      {
        context.pushWarning( QObject::tr( "%1: Skipping unsupported fill-color type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonFillColor.type() ) ) );
        break;
      }
    }
  }
  else
  {
    // defaults to #000000
    fillColor = QColor( 0, 0, 0 );
  }

  QColor fillOutlineColor;
  if ( !jsonPaint.contains( QStringLiteral( "fill-outline-color" ) ) )
  {
    // fill-outline-color
    if ( fillColor.isValid() )
      fillOutlineColor = fillColor;
    else
    {
      // use fill color data defined property
      if ( ddProperties.isActive( QgsSymbolLayer::PropertyFillColor ) )
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor,  ddProperties.property( QgsSymbolLayer::PropertyFillColor ) );
    }
  }
  else
  {
    const QVariant jsonFillOutlineColor = jsonPaint.value( QStringLiteral( "fill-outline-color" ) );
    switch ( jsonFillOutlineColor.type() )
    {
      case QVariant::Map:
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, parseInterpolateColorByZoom( jsonFillOutlineColor.toMap(), context, &fillOutlineColor ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, parseValueList( jsonFillOutlineColor.toList(), PropertyType::Color, context, 1, 255, &fillOutlineColor ) );
        break;

      case QVariant::String:
        fillOutlineColor = parseColor( jsonFillOutlineColor.toString(), context );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported fill-outline-color type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonFillOutlineColor.type() ) ) );
        break;
    }
  }

  double fillOpacity = -1.0;
  double rasterOpacity = -1.0;
  if ( jsonPaint.contains( QStringLiteral( "fill-opacity" ) ) )
  {
    const QVariant jsonFillOpacity = jsonPaint.value( QStringLiteral( "fill-opacity" ) );
    switch ( jsonFillOpacity.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        fillOpacity = jsonFillOpacity.toDouble();
        rasterOpacity = fillOpacity;
        break;

      case QVariant::Map:
        if ( ddProperties.isActive( QgsSymbolLayer::PropertyFillColor ) )
        {
          context.pushWarning( QObject::tr( "%1: Could not set opacity of layer, opacity already defined in fill color" ).arg( context.layerId() ) );
        }
        else
        {
          ddProperties.setProperty( QgsSymbolLayer::PropertyFillColor, parseInterpolateOpacityByZoom( jsonFillOpacity.toMap(), fillColor.isValid() ? fillColor.alpha() : 255 ) );
          ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, parseInterpolateOpacityByZoom( jsonFillOpacity.toMap(), fillOutlineColor.isValid() ? fillOutlineColor.alpha() : 255 ) );
          ddRasterProperties.setProperty( QgsSymbolLayer::PropertyOpacity, parseInterpolateByZoom( jsonFillOpacity.toMap(), context, 100, &rasterOpacity ) );
        }
        break;

      case QVariant::List:
      case QVariant::StringList:
        if ( ddProperties.isActive( QgsSymbolLayer::PropertyFillColor ) )
        {
          context.pushWarning( QObject::tr( "%1: Could not set opacity of layer, opacity already defined in fill color" ).arg( context.layerId() ) );
        }
        else
        {
          ddProperties.setProperty( QgsSymbolLayer::PropertyFillColor, parseValueList( jsonFillOpacity.toList(), PropertyType::Opacity, context, 1, fillColor.isValid() ? fillColor.alpha() : 255 ) );
          ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, parseValueList( jsonFillOpacity.toList(), PropertyType::Opacity, context, 1, fillOutlineColor.isValid() ? fillOutlineColor.alpha() : 255 ) );
          ddRasterProperties.setProperty( QgsSymbolLayer::PropertyOpacity, parseValueList( jsonFillOpacity.toList(), PropertyType::Numeric, context, 100, 255, nullptr, &rasterOpacity ) );
        }
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported fill-opacity type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonFillOpacity.type() ) ) );
        break;
    }
  }

  // fill-translate
  QPointF fillTranslate;
  if ( jsonPaint.contains( QStringLiteral( "fill-translate" ) ) )
  {
    const QVariant jsonFillTranslate = jsonPaint.value( QStringLiteral( "fill-translate" ) );
    switch ( jsonFillTranslate.type() )
    {

      case QVariant::Map:
        ddProperties.setProperty( QgsSymbolLayer::PropertyOffset, parseInterpolatePointByZoom( jsonFillTranslate.toMap(), context, context.pixelSizeConversionFactor(), &fillTranslate ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        fillTranslate = QPointF( jsonFillTranslate.toList().value( 0 ).toDouble() * context.pixelSizeConversionFactor(),
                                 jsonFillTranslate.toList().value( 1 ).toDouble() * context.pixelSizeConversionFactor() );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported fill-translate type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonFillTranslate.type() ) ) );
        break;
    }
  }

  std::unique_ptr< QgsSymbol > symbol( std::make_unique< QgsFillSymbol >() );
  QgsSimpleFillSymbolLayer *fillSymbol = dynamic_cast< QgsSimpleFillSymbolLayer * >( symbol->symbolLayer( 0 ) );
  Q_ASSERT( fillSymbol ); // should not fail since QgsFillSymbol() constructor instantiates a QgsSimpleFillSymbolLayer

  // set render units
  symbol->setOutputUnit( context.targetUnit() );
  fillSymbol->setOutputUnit( context.targetUnit() );

  if ( !fillTranslate.isNull() )
  {
    fillSymbol->setOffset( fillTranslate );
  }
  fillSymbol->setOffsetUnit( context.targetUnit() );

  if ( jsonPaint.contains( QStringLiteral( "fill-pattern" ) ) )
  {
    // get fill-pattern to set sprite

    const QVariant fillPatternJson = jsonPaint.value( QStringLiteral( "fill-pattern" ) );

    // fill-pattern disabled dillcolor
    fillColor = QColor();
    fillOutlineColor = QColor();

    // fill-pattern can be String or Object
    // String: {"fill-pattern": "dash-t"}
    // Object: {"fill-pattern":{"stops":[[11,"wetland8"],[12,"wetland16"]]}}

    QSize spriteSize;
    QString spriteProperty, spriteSizeProperty;
    const QString sprite = retrieveSpriteAsBase64( fillPatternJson, context, spriteSize, spriteProperty, spriteSizeProperty );
    if ( !sprite.isEmpty() )
    {
      // when fill-pattern exists, set and insert QgsRasterFillSymbolLayer
      QgsRasterFillSymbolLayer *rasterFill = new QgsRasterFillSymbolLayer();
      rasterFill->setImageFilePath( sprite );
      rasterFill->setWidth( spriteSize.width() );
      rasterFill->setWidthUnit( context.targetUnit() );
      rasterFill->setCoordinateMode( QgsRasterFillSymbolLayer::Viewport );

      if ( rasterOpacity >= 0 )
      {
        rasterFill->setOpacity( rasterOpacity );
      }

      if ( !spriteProperty.isEmpty() )
      {
        ddRasterProperties.setProperty( QgsSymbolLayer::PropertyFile, QgsProperty::fromExpression( spriteProperty ) );
        ddRasterProperties.setProperty( QgsSymbolLayer::PropertyWidth, QgsProperty::fromExpression( spriteSizeProperty ) );
      }

      rasterFill->setDataDefinedProperties( ddRasterProperties );
      symbol->appendSymbolLayer( rasterFill );
    }
  }

  fillSymbol->setDataDefinedProperties( ddProperties );

  if ( fillOpacity != -1 )
  {
    symbol->setOpacity( fillOpacity );
  }

  if ( fillOutlineColor.isValid() )
  {
    fillSymbol->setStrokeColor( fillOutlineColor );
  }
  else
  {
    fillSymbol->setStrokeStyle( Qt::NoPen );
  }

  if ( fillColor.isValid() )
  {
    fillSymbol->setFillColor( fillColor );
  }
  else
  {
    fillSymbol->setBrushStyle( Qt::NoBrush );
  }

  style.setGeometryType( QgsWkbTypes::PolygonGeometry );
  style.setSymbol( symbol.release() );
  return true;
}

bool QgsMapBoxGlStyleConverter::parseLineLayer( const QVariantMap &jsonLayer, QgsVectorTileBasicRendererStyle &style, QgsMapBoxGlStyleConversionContext &context )
{
  if ( !jsonLayer.contains( QStringLiteral( "paint" ) ) )
  {
    context.pushWarning( QObject::tr( "%1: Style has no paint property, skipping" ).arg( context.layerId() ) );
    return false;
  }

  const QVariantMap jsonPaint = jsonLayer.value( QStringLiteral( "paint" ) ).toMap();
  if ( jsonPaint.contains( QStringLiteral( "line-pattern" ) ) )
  {
    context.pushWarning( QObject::tr( "%1: Skipping unsupported line-pattern property" ).arg( context.layerId() ) );
    return false;
  }

  QgsPropertyCollection ddProperties;

  // line color
  QColor lineColor;
  if ( jsonPaint.contains( QStringLiteral( "line-color" ) ) )
  {
    const QVariant jsonLineColor = jsonPaint.value( QStringLiteral( "line-color" ) );
    switch ( jsonLineColor.type() )
    {
      case QVariant::Map:
        ddProperties.setProperty( QgsSymbolLayer::PropertyFillColor, parseInterpolateColorByZoom( jsonLineColor.toMap(), context, &lineColor ) );
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, ddProperties.property( QgsSymbolLayer::PropertyFillColor ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddProperties.setProperty( QgsSymbolLayer::PropertyFillColor, parseValueList( jsonLineColor.toList(), PropertyType::Color, context, 1, 255, &lineColor ) );
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, ddProperties.property( QgsSymbolLayer::PropertyFillColor ) );
        break;

      case QVariant::String:
        lineColor = parseColor( jsonLineColor.toString(), context );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported line-color type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonLineColor.type() ) ) );
        break;
    }
  }
  else
  {
    // defaults to #000000
    lineColor = QColor( 0, 0, 0 );
  }


  double lineWidth = 1.0;
  if ( jsonPaint.contains( QStringLiteral( "line-width" ) ) )
  {
    const QVariant jsonLineWidth = jsonPaint.value( QStringLiteral( "line-width" ) );
    switch ( jsonLineWidth.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        lineWidth = jsonLineWidth.toDouble() * context.pixelSizeConversionFactor();
        break;

      case QVariant::Map:
        lineWidth = -1;
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeWidth, parseInterpolateByZoom( jsonLineWidth.toMap(), context, context.pixelSizeConversionFactor(), &lineWidth ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeWidth, parseValueList( jsonLineWidth.toList(), PropertyType::Numeric, context, context.pixelSizeConversionFactor(), 255, nullptr, &lineWidth ) );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported fill-width type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonLineWidth.type() ) ) );
        break;
    }
  }

  double lineOffset = 0.0;
  if ( jsonPaint.contains( QStringLiteral( "line-offset" ) ) )
  {
    const QVariant jsonLineOffset = jsonPaint.value( QStringLiteral( "line-offset" ) );
    switch ( jsonLineOffset.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        lineOffset = -jsonLineOffset.toDouble() * context.pixelSizeConversionFactor();
        break;

      case QVariant::Map:
        lineWidth = -1;
        ddProperties.setProperty( QgsSymbolLayer::PropertyOffset, parseInterpolateByZoom( jsonLineOffset.toMap(), context, context.pixelSizeConversionFactor() * -1, &lineOffset ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddProperties.setProperty( QgsSymbolLayer::PropertyOffset, parseValueList( jsonLineOffset.toList(), PropertyType::Numeric, context, context.pixelSizeConversionFactor() * -1, 255, nullptr, &lineOffset ) );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported line-offset type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonLineOffset.type() ) ) );
        break;
    }
  }

  double lineOpacity = -1.0;
  if ( jsonPaint.contains( QStringLiteral( "line-opacity" ) ) )
  {
    const QVariant jsonLineOpacity = jsonPaint.value( QStringLiteral( "line-opacity" ) );
    switch ( jsonLineOpacity.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        lineOpacity = jsonLineOpacity.toDouble();
        break;

      case QVariant::Map:
        if ( ddProperties.isActive( QgsSymbolLayer::PropertyStrokeColor ) )
        {
          context.pushWarning( QObject::tr( "%1: Could not set opacity of layer, opacity already defined in stroke color" ).arg( context.layerId() ) );
        }
        else
        {
          ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, parseInterpolateOpacityByZoom( jsonLineOpacity.toMap(), lineColor.isValid() ? lineColor.alpha() : 255 ) );
        }
        break;

      case QVariant::List:
      case QVariant::StringList:
        if ( ddProperties.isActive( QgsSymbolLayer::PropertyStrokeColor ) )
        {
          context.pushWarning( QObject::tr( "%1: Could not set opacity of layer, opacity already defined in stroke color" ).arg( context.layerId() ) );
        }
        else
        {
          ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, parseValueList( jsonLineOpacity.toList(), PropertyType::Opacity, context, 1, lineColor.isValid() ? lineColor.alpha() : 255 ) );
        }
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported line-opacity type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonLineOpacity.type() ) ) );
        break;
    }
  }

  QVector< double > dashVector;
  if ( jsonPaint.contains( QStringLiteral( "line-dasharray" ) ) )
  {
    const QVariant jsonLineDashArray = jsonPaint.value( QStringLiteral( "line-dasharray" ) );
    switch ( jsonLineDashArray.type() )
    {
      case QVariant::Map:
      {
        //TODO improve parsing (use PropertyCustomDash?)
        const QVariantList dashSource = jsonLineDashArray.toMap().value( QStringLiteral( "stops" ) ).toList().last().toList().value( 1 ).toList();
        for ( const QVariant &v : dashSource )
        {
          dashVector << v.toDouble() * context.pixelSizeConversionFactor();
        }
        break;
      }

      case QVariant::List:
      case QVariant::StringList:
      {
        const QVariantList dashSource = jsonLineDashArray.toList();
        for ( const QVariant &v : dashSource )
        {
          dashVector << v.toDouble() * context.pixelSizeConversionFactor();
        }
        break;
      }

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported line-dasharray type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonLineDashArray.type() ) ) );
        break;
    }
  }

  Qt::PenCapStyle penCapStyle = Qt::FlatCap;
  Qt::PenJoinStyle penJoinStyle = Qt::MiterJoin;
  if ( jsonLayer.contains( QStringLiteral( "layout" ) ) )
  {
    const QVariantMap jsonLayout = jsonLayer.value( QStringLiteral( "layout" ) ).toMap();
    if ( jsonLayout.contains( QStringLiteral( "line-cap" ) ) )
    {
      penCapStyle = parseCapStyle( jsonLayout.value( QStringLiteral( "line-cap" ) ).toString() );
    }
    if ( jsonLayout.contains( QStringLiteral( "line-join" ) ) )
    {
      penJoinStyle = parseJoinStyle( jsonLayout.value( QStringLiteral( "line-join" ) ).toString() );
    }
  }

  std::unique_ptr< QgsSymbol > symbol( std::make_unique< QgsLineSymbol >() );
  QgsSimpleLineSymbolLayer *lineSymbol = dynamic_cast< QgsSimpleLineSymbolLayer * >( symbol->symbolLayer( 0 ) );
  Q_ASSERT( lineSymbol ); // should not fail since QgsLineSymbol() constructor instantiates a QgsSimpleLineSymbolLayer

  // set render units
  symbol->setOutputUnit( context.targetUnit() );
  lineSymbol->setOutputUnit( context.targetUnit() );
  lineSymbol->setPenCapStyle( penCapStyle );
  lineSymbol->setPenJoinStyle( penJoinStyle );
  lineSymbol->setDataDefinedProperties( ddProperties );
  lineSymbol->setOffset( lineOffset );
  lineSymbol->setOffsetUnit( context.targetUnit() );

  if ( lineOpacity != -1 )
  {
    symbol->setOpacity( lineOpacity );
  }
  if ( lineColor.isValid() )
  {
    lineSymbol->setColor( lineColor );
  }
  if ( lineWidth != -1 )
  {
    lineSymbol->setWidth( lineWidth );
  }
  if ( !dashVector.empty() )
  {
    lineSymbol->setUseCustomDashPattern( true );
    lineSymbol->setCustomDashVector( dashVector );
  }

  style.setGeometryType( QgsWkbTypes::LineGeometry );
  style.setSymbol( symbol.release() );
  return true;
}

bool QgsMapBoxGlStyleConverter::parseCircleLayer( const QVariantMap &jsonLayer, QgsVectorTileBasicRendererStyle &style, QgsMapBoxGlStyleConversionContext &context )
{
  if ( !jsonLayer.contains( QStringLiteral( "paint" ) ) )
  {
    context.pushWarning( QObject::tr( "%1: Style has no paint property, skipping" ).arg( context.layerId() ) );
    return false;
  }

  const QVariantMap jsonPaint = jsonLayer.value( QStringLiteral( "paint" ) ).toMap();
  QgsPropertyCollection ddProperties;

  // circle color
  QColor circleFillColor;
  if ( jsonPaint.contains( QStringLiteral( "circle-color" ) ) )
  {
    const QVariant jsonCircleColor = jsonPaint.value( QStringLiteral( "circle-color" ) );
    switch ( jsonCircleColor.type() )
    {
      case QVariant::Map:
        ddProperties.setProperty( QgsSymbolLayer::PropertyFillColor, parseInterpolateColorByZoom( jsonCircleColor.toMap(), context, &circleFillColor ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddProperties.setProperty( QgsSymbolLayer::PropertyFillColor, parseValueList( jsonCircleColor.toList(), PropertyType::Color, context, 1, 255, &circleFillColor ) );
        break;

      case QVariant::String:
        circleFillColor = parseColor( jsonCircleColor.toString(), context );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported circle-color type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonCircleColor.type() ) ) );
        break;
    }
  }
  else
  {
    // defaults to #000000
    circleFillColor = QColor( 0, 0, 0 );
  }

  // circle radius
  double circleDiameter = 10.0;
  if ( jsonPaint.contains( QStringLiteral( "circle-radius" ) ) )
  {
    const QVariant jsonCircleRadius = jsonPaint.value( QStringLiteral( "circle-radius" ) );
    switch ( jsonCircleRadius.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        circleDiameter = jsonCircleRadius.toDouble() * context.pixelSizeConversionFactor() * 2;
        break;

      case QVariant::Map:
        circleDiameter = -1;
        ddProperties.setProperty( QgsSymbolLayer::PropertyWidth, parseInterpolateByZoom( jsonCircleRadius.toMap(), context, context.pixelSizeConversionFactor() * 2, &circleDiameter ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddProperties.setProperty( QgsSymbolLayer::PropertyWidth, parseValueList( jsonCircleRadius.toList(), PropertyType::Numeric, context, context.pixelSizeConversionFactor() * 2, 255, nullptr, &circleDiameter ) );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported circle-radius type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonCircleRadius.type() ) ) );
        break;
    }
  }

  double circleOpacity = -1.0;
  if ( jsonPaint.contains( QStringLiteral( "circle-opacity" ) ) )
  {
    const QVariant jsonCircleOpacity = jsonPaint.value( QStringLiteral( "circle-opacity" ) );
    switch ( jsonCircleOpacity.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        circleOpacity = jsonCircleOpacity.toDouble();
        break;

      case QVariant::Map:
        ddProperties.setProperty( QgsSymbolLayer::PropertyFillColor, parseInterpolateOpacityByZoom( jsonCircleOpacity.toMap(), circleFillColor.isValid() ? circleFillColor.alpha() : 255 ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddProperties.setProperty( QgsSymbolLayer::PropertyFillColor, parseValueList( jsonCircleOpacity.toList(), PropertyType::Opacity, context, 1, circleFillColor.isValid() ? circleFillColor.alpha() : 255 ) );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported circle-opacity type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonCircleOpacity.type() ) ) );
        break;
    }
  }
  if ( ( circleOpacity != -1 ) && circleFillColor.isValid() )
  {
    circleFillColor.setAlphaF( circleOpacity );
  }

  // circle stroke color
  QColor circleStrokeColor;
  if ( jsonPaint.contains( QStringLiteral( "circle-stroke-color" ) ) )
  {
    const QVariant jsonCircleStrokeColor = jsonPaint.value( QStringLiteral( "circle-stroke-color" ) );
    switch ( jsonCircleStrokeColor.type() )
    {
      case QVariant::Map:
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, parseInterpolateColorByZoom( jsonCircleStrokeColor.toMap(), context, &circleStrokeColor ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, parseValueList( jsonCircleStrokeColor.toList(), PropertyType::Color, context, 1, 255, &circleStrokeColor ) );
        break;

      case QVariant::String:
        circleStrokeColor = parseColor( jsonCircleStrokeColor.toString(), context );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported circle-stroke-color type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonCircleStrokeColor.type() ) ) );
        break;
    }
  }

  // circle stroke width
  double circleStrokeWidth = -1.0;
  if ( jsonPaint.contains( QStringLiteral( "circle-stroke-width" ) ) )
  {
    const QVariant circleStrokeWidthJson = jsonPaint.value( QStringLiteral( "circle-stroke-width" ) );
    switch ( circleStrokeWidthJson.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        circleStrokeWidth = circleStrokeWidthJson.toDouble() * context.pixelSizeConversionFactor();
        break;

      case QVariant::Map:
        circleStrokeWidth = -1.0;
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeWidth, parseInterpolateByZoom( circleStrokeWidthJson.toMap(), context, context.pixelSizeConversionFactor(), &circleStrokeWidth ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeWidth, parseValueList( circleStrokeWidthJson.toList(), PropertyType::Numeric, context, context.pixelSizeConversionFactor(), 255, nullptr, &circleStrokeWidth ) );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported circle-stroke-width type (%2)" ).arg( context.layerId(), QMetaType::typeName( circleStrokeWidthJson.type() ) ) );
        break;
    }
  }

  double circleStrokeOpacity = -1.0;
  if ( jsonPaint.contains( QStringLiteral( "circle-stroke-opacity" ) ) )
  {
    const QVariant jsonCircleStrokeOpacity = jsonPaint.value( QStringLiteral( "circle-stroke-opacity" ) );
    switch ( jsonCircleStrokeOpacity.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        circleStrokeOpacity = jsonCircleStrokeOpacity.toDouble();
        break;

      case QVariant::Map:
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, parseInterpolateOpacityByZoom( jsonCircleStrokeOpacity.toMap(), circleStrokeColor.isValid() ? circleStrokeColor.alpha() : 255 ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddProperties.setProperty( QgsSymbolLayer::PropertyStrokeColor, parseValueList( jsonCircleStrokeOpacity.toList(), PropertyType::Opacity, context, 1, circleStrokeColor.isValid() ? circleStrokeColor.alpha() : 255 ) );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported circle-stroke-opacity type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonCircleStrokeOpacity.type() ) ) );
        break;
    }
  }
  if ( ( circleStrokeOpacity != -1 ) && circleStrokeColor.isValid() )
  {
    circleStrokeColor.setAlphaF( circleStrokeOpacity );
  }

  // translate
  QPointF circleTranslate;
  if ( jsonPaint.contains( QStringLiteral( "circle-translate" ) ) )
  {
    const QVariant jsonCircleTranslate = jsonPaint.value( QStringLiteral( "circle-translate" ) );
    switch ( jsonCircleTranslate.type() )
    {

      case QVariant::Map:
        ddProperties.setProperty( QgsSymbolLayer::PropertyOffset, parseInterpolatePointByZoom( jsonCircleTranslate.toMap(), context, context.pixelSizeConversionFactor(), &circleTranslate ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        circleTranslate = QPointF( jsonCircleTranslate.toList().value( 0 ).toDouble() * context.pixelSizeConversionFactor(),
                                   jsonCircleTranslate.toList().value( 1 ).toDouble() * context.pixelSizeConversionFactor() );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported circle-translate type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonCircleTranslate.type() ) ) );
        break;
    }
  }

  std::unique_ptr< QgsSymbol > symbol( std::make_unique< QgsMarkerSymbol >() );
  QgsSimpleMarkerSymbolLayer *markerSymbolLayer = dynamic_cast< QgsSimpleMarkerSymbolLayer * >( symbol->symbolLayer( 0 ) );
  Q_ASSERT( markerSymbolLayer );

  // set render units
  symbol->setOutputUnit( context.targetUnit() );
  symbol->setDataDefinedProperties( ddProperties );

  if ( !circleTranslate.isNull() )
  {
    markerSymbolLayer->setOffset( circleTranslate );
    markerSymbolLayer->setOffsetUnit( context.targetUnit() );
  }

  if ( circleFillColor.isValid() )
  {
    markerSymbolLayer->setFillColor( circleFillColor );
  }
  if ( circleDiameter != -1 )
  {
    markerSymbolLayer->setSize( circleDiameter );
    markerSymbolLayer->setSizeUnit( context.targetUnit() );
  }
  if ( circleStrokeColor.isValid() )
  {
    markerSymbolLayer->setStrokeColor( circleStrokeColor );
  }
  if ( circleStrokeWidth != -1 )
  {
    markerSymbolLayer->setStrokeWidth( circleStrokeWidth );
    markerSymbolLayer->setStrokeWidthUnit( context.targetUnit() );
  }

  style.setGeometryType( QgsWkbTypes::PointGeometry );
  style.setSymbol( symbol.release() );
  return true;
}

void QgsMapBoxGlStyleConverter::parseSymbolLayer( const QVariantMap &jsonLayer, QgsVectorTileBasicRendererStyle &renderer, bool &hasRenderer, QgsVectorTileBasicLabelingStyle &labelingStyle, bool &hasLabeling, QgsMapBoxGlStyleConversionContext &context )
{
  hasLabeling = false;
  hasRenderer = false;

  if ( !jsonLayer.contains( QStringLiteral( "layout" ) ) )
  {
    context.pushWarning( QObject::tr( "%1: Style layer has no layout property, skipping" ).arg( context.layerId() ) );
    return;
  }
  const QVariantMap jsonLayout = jsonLayer.value( QStringLiteral( "layout" ) ).toMap();
  if ( !jsonLayout.contains( QStringLiteral( "text-field" ) ) )
  {
    hasRenderer = parseSymbolLayerAsRenderer( jsonLayer, renderer, context );
    return;
  }

  if ( !jsonLayer.contains( QStringLiteral( "paint" ) ) )
  {
    context.pushWarning( QObject::tr( "%1: Style layer has no paint property, skipping" ).arg( context.layerId() ) );
    return;
  }
  const QVariantMap jsonPaint = jsonLayer.value( QStringLiteral( "paint" ) ).toMap();

  QgsPropertyCollection ddLabelProperties;

  double textSize = 16.0 * context.pixelSizeConversionFactor();
  QgsProperty textSizeProperty;
  if ( jsonLayout.contains( QStringLiteral( "text-size" ) ) )
  {
    const QVariant jsonTextSize = jsonLayout.value( QStringLiteral( "text-size" ) );
    switch ( jsonTextSize.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        textSize = jsonTextSize.toDouble() * context.pixelSizeConversionFactor();
        break;

      case QVariant::Map:
        textSize = -1;
        textSizeProperty = parseInterpolateByZoom( jsonTextSize.toMap(), context, context.pixelSizeConversionFactor(), &textSize );

        break;

      case QVariant::List:
      case QVariant::StringList:
        textSize = -1;
        textSizeProperty = parseValueList( jsonTextSize.toList(), PropertyType::Numeric, context, context.pixelSizeConversionFactor(), 255, nullptr, &textSize );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported text-size type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonTextSize.type() ) ) );
        break;
    }

    if ( textSizeProperty )
    {
      ddLabelProperties.setProperty( QgsPalLayerSettings::Size, textSizeProperty );
    }
  }

  // a rough average of ems to character count conversion for a variety of fonts
  constexpr double EM_TO_CHARS = 2.0;

  double textMaxWidth = -1;
  if ( jsonLayout.contains( QStringLiteral( "text-max-width" ) ) )
  {
    const QVariant jsonTextMaxWidth = jsonLayout.value( QStringLiteral( "text-max-width" ) );
    switch ( jsonTextMaxWidth.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        textMaxWidth = jsonTextMaxWidth.toDouble() * EM_TO_CHARS;
        break;

      case QVariant::Map:
        ddLabelProperties.setProperty( QgsPalLayerSettings::AutoWrapLength, parseInterpolateByZoom( jsonTextMaxWidth.toMap(), context, EM_TO_CHARS, &textMaxWidth ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddLabelProperties.setProperty( QgsPalLayerSettings::AutoWrapLength, parseValueList( jsonTextMaxWidth.toList(), PropertyType::Numeric, context, EM_TO_CHARS, 255, nullptr, &textMaxWidth ) );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported text-max-width type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonTextMaxWidth.type() ) ) );
        break;
    }
  }
  else
  {
    // defaults to 10
    textMaxWidth = 10 * EM_TO_CHARS;
  }

  double textLetterSpacing = -1;
  if ( jsonLayout.contains( QStringLiteral( "text-letter-spacing" ) ) )
  {
    const QVariant jsonTextLetterSpacing = jsonLayout.value( QStringLiteral( "text-letter-spacing" ) );
    switch ( jsonTextLetterSpacing.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        textLetterSpacing = jsonTextLetterSpacing.toDouble();
        break;

      case QVariant::Map:
        ddLabelProperties.setProperty( QgsPalLayerSettings::FontLetterSpacing, parseInterpolateByZoom( jsonTextLetterSpacing.toMap(), context, 1, &textLetterSpacing ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddLabelProperties.setProperty( QgsPalLayerSettings::FontLetterSpacing, parseValueList( jsonTextLetterSpacing.toList(), PropertyType::Numeric, context, 1, 255, nullptr, &textLetterSpacing ) );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported text-letter-spacing type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonTextLetterSpacing.type() ) ) );
        break;
    }
  }

  QFont textFont;
  bool foundFont = false;
  QString fontName;
  if ( jsonLayout.contains( QStringLiteral( "text-font" ) ) )
  {
    auto splitFontFamily = []( const QString & fontName, QString & family, QString & style ) -> bool
    {
      const QStringList textFontParts = fontName.split( ' ' );
      for ( int i = 1; i < textFontParts.size(); ++i )
      {
        const QString candidateFontName = textFontParts.mid( 0, i ).join( ' ' );
        const QString candidateFontStyle = textFontParts.mid( i ).join( ' ' );
        if ( QgsFontUtils::fontFamilyHasStyle( candidateFontName, candidateFontStyle ) )
        {
          family = candidateFontName;
          style = candidateFontStyle;
          return true;
        }
      }

      if ( QFontDatabase().hasFamily( fontName ) )
      {
        // the json isn't following the spec correctly!!
        family = fontName;
        style.clear();
        return true;
      }
      return false;
    };

    const QVariant jsonTextFont = jsonLayout.value( QStringLiteral( "text-font" ) );
    if ( jsonTextFont.type() != QVariant::List && jsonTextFont.type() != QVariant::StringList && jsonTextFont.type() != QVariant::String
         && jsonTextFont.type() != QVariant::Map )
    {
      context.pushWarning( QObject::tr( "%1: Skipping unsupported text-font type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonTextFont.type() ) ) );
    }
    else
    {
      switch ( jsonTextFont.type() )
      {
        case QVariant::List:
        case QVariant::StringList:
          fontName = jsonTextFont.toList().value( 0 ).toString();
          break;

        case QVariant::String:
          fontName = jsonTextFont.toString();
          break;

        case QVariant::Map:
        {
          QString familyCaseString = QStringLiteral( "CASE " );
          QString styleCaseString = QStringLiteral( "CASE " );
          QString fontFamily;
          QString fontStyle;
          const QVariantList stops = jsonTextFont.toMap().value( QStringLiteral( "stops" ) ).toList();

          bool error = false;
          for ( int i = 0; i < stops.length() - 1; ++i )
          {
            // bottom zoom and value
            const QVariant bz = stops.value( i ).toList().value( 0 );
            const QString bv = stops.value( i ).toList().value( 1 ).type() == QVariant::String ? stops.value( i ).toList().value( 1 ).toString() : stops.value( i ).toList().value( 1 ).toList().value( 0 ).toString();
            if ( bz.type() == QVariant::List || bz.type() == QVariant::StringList )
            {
              context.pushWarning( QObject::tr( "%1: Expressions in interpolation function are not supported, skipping." ).arg( context.layerId() ) );
              error = true;
              break;
            }

            // top zoom
            const QVariant tz = stops.value( i + 1 ).toList().value( 0 );
            if ( tz.type() == QVariant::List || tz.type() == QVariant::StringList )
            {
              context.pushWarning( QObject::tr( "%1: Expressions in interpolation function are not supported, skipping." ).arg( context.layerId() ) );
              error = true;
              break;
            }

            if ( splitFontFamily( bv, fontFamily, fontStyle ) )
            {
              familyCaseString += QStringLiteral( "WHEN @vector_tile_zoom > %1 AND @vector_tile_zoom <= %2 "
                                                  "THEN %3 " ).arg( bz.toString(),
                                                      tz.toString(),
                                                      QgsExpression::quotedValue( fontFamily ) );
              styleCaseString += QStringLiteral( "WHEN @vector_tile_zoom > %1 AND @vector_tile_zoom <= %2 "
                                                 "THEN %3 " ).arg( bz.toString(),
                                                     tz.toString(),
                                                     QgsExpression::quotedValue( fontStyle ) );
            }
            else
            {
              context.pushWarning( QObject::tr( "%1: Referenced font %2 is not available on system" ).arg( context.layerId(), bv ) );
            }
          }
          if ( error )
            break;

          const QString bv = stops.constLast().toList().value( 1 ).type() == QVariant::String ? stops.constLast().toList().value( 1 ).toString() : stops.constLast().toList().value( 1 ).toList().value( 0 ).toString();
          if ( splitFontFamily( bv, fontFamily, fontStyle ) )
          {
            familyCaseString += QStringLiteral( "ELSE %1 END" ).arg( QgsExpression::quotedValue( fontFamily ) );
            styleCaseString += QStringLiteral( "ELSE %1 END" ).arg( QgsExpression::quotedValue( fontStyle ) );
          }
          else
          {
            context.pushWarning( QObject::tr( "%1: Referenced font %2 is not available on system" ).arg( context.layerId(), bv ) );
          }

          ddLabelProperties.setProperty( QgsPalLayerSettings::Family, QgsProperty::fromExpression( familyCaseString ) );
          ddLabelProperties.setProperty( QgsPalLayerSettings::FontStyle, QgsProperty::fromExpression( styleCaseString ) );

          foundFont = true;
          fontName = fontFamily;

          break;
        }

        default:
          break;
      }

      QString fontFamily;
      QString fontStyle;
      if ( splitFontFamily( fontName, fontFamily, fontStyle ) )
      {
        textFont = QFont( fontFamily );
        if ( !fontStyle.isEmpty() )
          textFont.setStyleName( fontStyle );
        foundFont = true;
      }
    }
  }
  else
  {
    // Defaults to ["Open Sans Regular","Arial Unicode MS Regular"].
    if ( QgsFontUtils::fontFamilyHasStyle( QStringLiteral( "Open Sans" ), QStringLiteral( "Regular" ) ) )
    {
      fontName = QStringLiteral( "Open Sans" );
      textFont = QFont( fontName );
      textFont.setStyleName( QStringLiteral( "Regular" ) );
      foundFont = true;
    }
    else if ( QgsFontUtils::fontFamilyHasStyle( QStringLiteral( "Arial Unicode MS" ), QStringLiteral( "Regular" ) ) )
    {
      fontName = QStringLiteral( "Arial Unicode MS" );
      textFont = QFont( fontName );
      textFont.setStyleName( QStringLiteral( "Regular" ) );
      foundFont = true;
    }
    else
    {
      fontName = QStringLiteral( "Open Sans, Arial Unicode MS" );
    }
  }
  if ( !foundFont && !fontName.isEmpty() )
  {
    context.pushWarning( QObject::tr( "%1: Referenced font %2 is not available on system" ).arg( context.layerId(), fontName ) );
  }

  // text color
  QColor textColor;
  if ( jsonPaint.contains( QStringLiteral( "text-color" ) ) )
  {
    const QVariant jsonTextColor = jsonPaint.value( QStringLiteral( "text-color" ) );
    switch ( jsonTextColor.type() )
    {
      case QVariant::Map:
        ddLabelProperties.setProperty( QgsPalLayerSettings::Color, parseInterpolateColorByZoom( jsonTextColor.toMap(), context, &textColor ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddLabelProperties.setProperty( QgsPalLayerSettings::Color, parseValueList( jsonTextColor.toList(), PropertyType::Color, context, 1, 255, &textColor ) );
        break;

      case QVariant::String:
        textColor = parseColor( jsonTextColor.toString(), context );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported text-color type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonTextColor.type() ) ) );
        break;
    }
  }
  else
  {
    // defaults to #000000
    textColor = QColor( 0, 0, 0 );
  }

  // buffer color
  QColor bufferColor;
  if ( jsonPaint.contains( QStringLiteral( "text-halo-color" ) ) )
  {
    const QVariant jsonBufferColor = jsonPaint.value( QStringLiteral( "text-halo-color" ) );
    switch ( jsonBufferColor.type() )
    {
      case QVariant::Map:
        ddLabelProperties.setProperty( QgsPalLayerSettings::BufferColor, parseInterpolateColorByZoom( jsonBufferColor.toMap(), context, &bufferColor ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        ddLabelProperties.setProperty( QgsPalLayerSettings::BufferColor, parseValueList( jsonBufferColor.toList(), PropertyType::Color, context, 1, 255, &bufferColor ) );
        break;

      case QVariant::String:
        bufferColor = parseColor( jsonBufferColor.toString(), context );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported text-halo-color type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonBufferColor.type() ) ) );
        break;
    }
  }

  double bufferSize = 0.0;
  // the pixel based text buffers appear larger when rendered on the web - so automatically scale
  // them up when converting to a QGIS style
  // (this number is based on trial-and-error comparisons only!)
  constexpr double BUFFER_SIZE_SCALE = 2.0;
  if ( jsonPaint.contains( QStringLiteral( "text-halo-width" ) ) )
  {
    const QVariant jsonHaloWidth = jsonPaint.value( QStringLiteral( "text-halo-width" ) );
    switch ( jsonHaloWidth.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
        bufferSize = jsonHaloWidth.toDouble() * context.pixelSizeConversionFactor() * BUFFER_SIZE_SCALE;
        break;

      case QVariant::Map:
        bufferSize = 1;
        ddLabelProperties.setProperty( QgsPalLayerSettings::BufferSize, parseInterpolateByZoom( jsonHaloWidth.toMap(), context, context.pixelSizeConversionFactor() * BUFFER_SIZE_SCALE, &bufferSize ) );
        break;

      case QVariant::List:
      case QVariant::StringList:
        bufferSize = 1;
        ddLabelProperties.setProperty( QgsPalLayerSettings::BufferSize, parseValueList( jsonHaloWidth.toList(), PropertyType::Numeric, context, context.pixelSizeConversionFactor() * BUFFER_SIZE_SCALE, 255, nullptr, &bufferSize ) );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported text-halo-width type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonHaloWidth.type() ) ) );
        break;
    }
  }

  double haloBlurSize = 0;
  if ( jsonPaint.contains( QStringLiteral( "text-halo-blur" ) ) )
  {
    const QVariant jsonTextHaloBlur = jsonPaint.value( QStringLiteral( "text-halo-blur" ) );
    switch ( jsonTextHaloBlur.type() )
    {
      case QVariant::Int:
      case QVariant::Double:
      {
        haloBlurSize = jsonTextHaloBlur.toDouble() * context.pixelSizeConversionFactor();
        break;
      }

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported text-halo-blur type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonTextHaloBlur.type() ) ) );
        break;
    }
  }

  QgsTextFormat format;
  format.setSizeUnit( context.targetUnit() );
  if ( textColor.isValid() )
    format.setColor( textColor );
  if ( textSize >= 0 )
    format.setSize( textSize );
  if ( foundFont )
    format.setFont( textFont );
  if ( textLetterSpacing > 0 )
  {
    QFont f = format.font();
    f.setLetterSpacing( QFont::AbsoluteSpacing, textLetterSpacing );
    format.setFont( f );
  }

  if ( bufferSize > 0 )
  {
    format.buffer().setEnabled( true );
    format.buffer().setSize( bufferSize );
    format.buffer().setSizeUnit( context.targetUnit() );
    format.buffer().setColor( bufferColor );

    if ( haloBlurSize > 0 )
    {
      QgsEffectStack *stack = new QgsEffectStack();
      QgsBlurEffect *blur = new QgsBlurEffect() ;
      blur->setEnabled( true );
      blur->setBlurUnit( context.targetUnit() );
      blur->setBlurLevel( haloBlurSize );
      blur->setBlurMethod( QgsBlurEffect::StackBlur );
      stack->appendEffect( blur );
      stack->setEnabled( true );
      format.buffer().setPaintEffect( stack );
    }
  }

  QgsPalLayerSettings labelSettings;

  if ( textMaxWidth > 0 )
  {
    labelSettings.autoWrapLength = textMaxWidth;
  }

  // convert field name

  auto processLabelField = []( const QString & string, bool & isExpression )->QString
  {
    // {field_name} is permitted in string -- if multiple fields are present, convert them to an expression
    // but if single field is covered in {}, return it directly
    const QRegularExpression singleFieldRx( QStringLiteral( "^{([^}]+)}$" ) );
    const QRegularExpressionMatch match = singleFieldRx.match( string );
    if ( match.hasMatch() )
    {
      isExpression = false;
      return match.captured( 1 );
    }

    const QRegularExpression multiFieldRx( QStringLiteral( "(?={[^}]+})" ) );
    const QStringList parts = string.split( multiFieldRx );
    if ( parts.size() > 1 )
    {
      isExpression = true;

      QStringList res;
      for ( const QString &part : parts )
      {
        if ( part.isEmpty() )
          continue;

        // part will start at a {field} reference
        const QStringList split = part.split( '}' );
        res << QgsExpression::quotedColumnRef( split.at( 0 ).mid( 1 ) );
        if ( !split.at( 1 ).isEmpty() )
          res << QgsExpression::quotedValue( split.at( 1 ) );
      }
      return QStringLiteral( "concat(%1)" ).arg( res.join( ',' ) );
    }
    else
    {
      isExpression = false;
      return string;
    }
  };

  if ( jsonLayout.contains( QStringLiteral( "text-field" ) ) )
  {
    const QVariant jsonTextField = jsonLayout.value( QStringLiteral( "text-field" ) );
    switch ( jsonTextField.type() )
    {
      case QVariant::String:
      {
        labelSettings.fieldName = processLabelField( jsonTextField.toString(), labelSettings.isExpression );
        break;
      }

      case QVariant::List:
      case QVariant::StringList:
      {
        const QVariantList textFieldList = jsonTextField.toList();
        /*
         * e.g.
         *     "text-field": ["format",
         *                    "foo", { "font-scale": 1.2 },
         *                    "bar", { "font-scale": 0.8 }
         * ]
         */
        if ( textFieldList.size() > 2 && textFieldList.at( 0 ).toString() == QLatin1String( "format" ) )
        {
          QStringList parts;
          for ( int i = 1; i < textFieldList.size(); ++i )
          {
            bool isExpression = false;
            const QString part = processLabelField( textFieldList.at( i ).toString(), isExpression );
            if ( !isExpression )
              parts << QgsExpression::quotedColumnRef( part );
            else
              parts << part;
            // TODO -- we could also translate font color, underline, overline, strikethrough to HTML tags!
            i += 1;
          }
          labelSettings.fieldName = QStringLiteral( "concat(%1)" ).arg( parts.join( ',' ) );
          labelSettings.isExpression = true;
        }
        else
        {
          /*
           * e.g.
           *     "text-field": ["to-string", ["get", "name"]]
           */
          labelSettings.fieldName = parseExpression( textFieldList, context );
          labelSettings.isExpression = true;
        }
        break;
      }

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported text-field type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonTextField.type() ) ) );
        break;
    }
  }

  if ( jsonLayout.contains( QStringLiteral( "text-transform" ) ) )
  {
    const QString textTransform = jsonLayout.value( QStringLiteral( "text-transform" ) ).toString();
    if ( textTransform == QLatin1String( "uppercase" ) )
    {
      labelSettings.fieldName = QStringLiteral( "upper(%1)" ).arg( labelSettings.isExpression ? labelSettings.fieldName : QgsExpression::quotedColumnRef( labelSettings.fieldName ) );
    }
    else if ( textTransform == QLatin1String( "lowercase" ) )
    {
      labelSettings.fieldName = QStringLiteral( "lower(%1)" ).arg( labelSettings.isExpression ? labelSettings.fieldName : QgsExpression::quotedColumnRef( labelSettings.fieldName ) );
    }
    labelSettings.isExpression = true;
  }

  labelSettings.placement = QgsPalLayerSettings::OverPoint;
  QgsWkbTypes::GeometryType geometryType = QgsWkbTypes::PointGeometry;
  if ( jsonLayout.contains( QStringLiteral( "symbol-placement" ) ) )
  {
    const QString symbolPlacement = jsonLayout.value( QStringLiteral( "symbol-placement" ) ).toString();
    if ( symbolPlacement == QLatin1String( "line" ) )
    {
      labelSettings.placement = QgsPalLayerSettings::Curved;
      labelSettings.lineSettings().setPlacementFlags( QgsLabeling::OnLine );
      geometryType = QgsWkbTypes::LineGeometry;

      if ( jsonLayout.contains( QStringLiteral( "text-rotation-alignment" ) ) )
      {
        const QString textRotationAlignment = jsonLayout.value( QStringLiteral( "text-rotation-alignment" ) ).toString();
        if ( textRotationAlignment == QLatin1String( "viewport" ) )
        {
          labelSettings.placement = QgsPalLayerSettings::Horizontal;
        }
      }

      if ( labelSettings.placement == QgsPalLayerSettings::Curved )
      {
        QPointF textOffset;
        QgsProperty textOffsetProperty;
        if ( jsonLayout.contains( QStringLiteral( "text-offset" ) ) )
        {
          const QVariant jsonTextOffset = jsonLayout.value( QStringLiteral( "text-offset" ) );

          // units are ems!
          switch ( jsonTextOffset.type() )
          {
            case QVariant::Map:
              textOffsetProperty = parseInterpolatePointByZoom( jsonTextOffset.toMap(), context, !textSizeProperty ? textSize : 1.0, &textOffset );
              if ( !textSizeProperty )
              {
                ddLabelProperties.setProperty( QgsPalLayerSettings::LabelDistance, QStringLiteral( "abs(array_get(%1,1))-%2" ).arg( textOffsetProperty ).arg( textSize ) );
              }
              else
              {
                ddLabelProperties.setProperty( QgsPalLayerSettings::LabelDistance, QStringLiteral( "with_variable('text_size',%2,abs(array_get(%1,1))*@text_size-@text_size)" ).arg( textOffsetProperty.asExpression(), textSizeProperty.asExpression() ) );
              }
              ddLabelProperties.setProperty( QgsPalLayerSettings::LinePlacementOptions, QStringLiteral( "if(array_get(%1,1)>0,'BL','AL')" ).arg( textOffsetProperty ) );
              break;

            case QVariant::List:
            case QVariant::StringList:
              textOffset = QPointF( jsonTextOffset.toList().value( 0 ).toDouble() * textSize,
                                    jsonTextOffset.toList().value( 1 ).toDouble() * textSize );
              break;

            default:
              context.pushWarning( QObject::tr( "%1: Skipping unsupported text-offset type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonTextOffset.type() ) ) );
              break;
          }

          if ( !textOffset.isNull() )
          {
            labelSettings.distUnits = context.targetUnit();
            labelSettings.dist = std::abs( textOffset.y() ) - textSize;
            labelSettings.lineSettings().setPlacementFlags( textOffset.y() > 0.0 ? QgsLabeling::BelowLine : QgsLabeling::AboveLine );
            if ( textSizeProperty && !textOffsetProperty )
            {
              ddLabelProperties.setProperty( QgsPalLayerSettings::LabelDistance, QStringLiteral( "with_variable('text_size',%2,%1*@text_size-@text_size)" ).arg( std::abs( textOffset.y() / textSize ) ).arg( textSizeProperty.asExpression() ) );
            }
          }
        }

        if ( textOffset.isNull() )
        {
          labelSettings.lineSettings().setPlacementFlags( QgsLabeling::OnLine );
        }
      }
    }
  }

  if ( jsonLayout.contains( QStringLiteral( "text-justify" ) ) )
  {
    const QVariant jsonTextJustify = jsonLayout.value( QStringLiteral( "text-justify" ) );

    // default is center
    QString textAlign = QStringLiteral( "center" );

    const QVariantMap conversionMap
    {
      { QStringLiteral( "left" ), QStringLiteral( "left" ) },
      { QStringLiteral( "center" ), QStringLiteral( "center" ) },
      { QStringLiteral( "right" ), QStringLiteral( "right" ) },
      { QStringLiteral( "auto" ), QStringLiteral( "follow" ) }
    };

    switch ( jsonTextJustify.type() )
    {
      case QVariant::String:
        textAlign = jsonTextJustify.toString();
        break;

      case QVariant::List:
        ddLabelProperties.setProperty( QgsPalLayerSettings::OffsetQuad, QgsProperty::fromExpression( parseStringStops( jsonTextJustify.toList(), context, conversionMap, &textAlign ) ) );
        break;

      case QVariant::Map:
        ddLabelProperties.setProperty( QgsPalLayerSettings::OffsetQuad, parseInterpolateStringByZoom( jsonTextJustify.toMap(), context, conversionMap, &textAlign ) );
        break;

      default:
        context.pushWarning( QObject::tr( "%1: Skipping unsupported text-justify type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonTextJustify.type() ) ) );
        break;
    }

    if ( textAlign == QLatin1String( "left" ) )
      labelSettings.multilineAlign = QgsPalLayerSettings::MultiLeft;
    else if ( textAlign == QLatin1String( "right" ) )
      labelSettings.multilineAlign = QgsPalLayerSettings::MultiRight;
    else if ( textAlign == QLatin1String( "center" ) )
      labelSettings.multilineAlign = QgsPalLayerSettings::MultiCenter;
    else if ( textAlign == QLatin1String( "follow" ) )
      labelSettings.multilineAlign = QgsPalLayerSettings::MultiFollowPlacement;
  }
  else
  {
    labelSettings.multilineAlign = QgsPalLayerSettings::MultiCenter;
  }

  if ( labelSettings.placement == QgsPalLayerSettings::OverPoint )
  {
    if ( jsonLayout.contains( QStringLiteral( "text-anchor" ) ) )
    {
      const QVariant jsonTextAnchor = jsonLayout.value( QStringLiteral( "text-anchor" ) );
      QString textAnchor;

      const QVariantMap conversionMap
      {
        { QStringLiteral( "center" ), 4 },
        { QStringLiteral( "left" ), 5 },
        { QStringLiteral( "right" ), 3 },
        { QStringLiteral( "top" ), 7 },
        { QStringLiteral( "bottom" ), 1 },
        { QStringLiteral( "top-left" ), 8 },
        { QStringLiteral( "top-right" ), 6 },
        { QStringLiteral( "bottom-left" ), 2 },
        { QStringLiteral( "bottom-right" ), 0 },
      };

      switch ( jsonTextAnchor.type() )
      {
        case QVariant::String:
          textAnchor = jsonTextAnchor.toString();
          break;

        case QVariant::List:
          ddLabelProperties.setProperty( QgsPalLayerSettings::OffsetQuad, QgsProperty::fromExpression( parseStringStops( jsonTextAnchor.toList(), context, conversionMap, &textAnchor ) ) );
          break;

        case QVariant::Map:
          ddLabelProperties.setProperty( QgsPalLayerSettings::OffsetQuad, parseInterpolateStringByZoom( jsonTextAnchor.toMap(), context, conversionMap, &textAnchor ) );
          break;

        default:
          context.pushWarning( QObject::tr( "%1: Skipping unsupported text-anchor type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonTextAnchor.type() ) ) );
          break;
      }

      if ( textAnchor == QLatin1String( "center" ) )
        labelSettings.quadOffset = QgsPalLayerSettings::QuadrantOver;
      else if ( textAnchor == QLatin1String( "left" ) )
        labelSettings.quadOffset = QgsPalLayerSettings::QuadrantRight;
      else if ( textAnchor == QLatin1String( "right" ) )
        labelSettings.quadOffset = QgsPalLayerSettings::QuadrantLeft;
      else if ( textAnchor == QLatin1String( "top" ) )
        labelSettings.quadOffset = QgsPalLayerSettings::QuadrantBelow;
      else if ( textAnchor == QLatin1String( "bottom" ) )
        labelSettings.quadOffset = QgsPalLayerSettings::QuadrantAbove;
      else if ( textAnchor == QLatin1String( "top-left" ) )
        labelSettings.quadOffset = QgsPalLayerSettings::QuadrantBelowRight;
      else if ( textAnchor == QLatin1String( "top-right" ) )
        labelSettings.quadOffset = QgsPalLayerSettings::QuadrantBelowLeft;
      else if ( textAnchor == QLatin1String( "bottom-left" ) )
        labelSettings.quadOffset = QgsPalLayerSettings::QuadrantAboveRight;
      else if ( textAnchor == QLatin1String( "bottom-right" ) )
        labelSettings.quadOffset = QgsPalLayerSettings::QuadrantAboveLeft;
    }

    QPointF textOffset;
    if ( jsonLayout.contains( QStringLiteral( "text-offset" ) ) )
    {
      const QVariant jsonTextOffset = jsonLayout.value( QStringLiteral( "text-offset" ) );

      // units are ems!
      switch ( jsonTextOffset.type() )
      {
        case QVariant::Map:
          ddLabelProperties.setProperty( QgsPalLayerSettings::OffsetXY, parseInterpolatePointByZoom( jsonTextOffset.toMap(), context, textSize, &textOffset ) );
          break;

        case QVariant::List:
        case QVariant::StringList:
          textOffset = QPointF( jsonTextOffset.toList().value( 0 ).toDouble() * textSize,
                                jsonTextOffset.toList().value( 1 ).toDouble() * textSize );
          break;

        default:
          context.pushWarning( QObject::tr( "%1: Skipping unsupported text-offset type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonTextOffset.type() ) ) );
          break;
      }

      if ( !textOffset.isNull() )
      {
        labelSettings.offsetUnits = context.targetUnit();
        labelSettings.xOffset = textOffset.x();
        labelSettings.yOffset = textOffset.y();
      }
    }
  }

  if ( jsonLayout.contains( QStringLiteral( "icon-image" ) ) &&
       ( labelSettings.placement == QgsPalLayerSettings::Horizontal || labelSettings.placement == QgsPalLayerSettings::Curved ) )
  {
    QSize spriteSize;
    QString spriteProperty, spriteSizeProperty;
    const QString sprite = retrieveSpriteAsBase64( jsonLayout.value( QStringLiteral( "icon-image" ) ), context, spriteSize, spriteProperty, spriteSizeProperty );
    if ( !sprite.isEmpty() )
    {
      QgsRasterMarkerSymbolLayer *markerLayer = new QgsRasterMarkerSymbolLayer( );
      markerLayer->setPath( sprite );
      markerLayer->setSize( spriteSize.width() );
      markerLayer->setSizeUnit( context.targetUnit() );

      if ( !spriteProperty.isEmpty() )
      {
        QgsPropertyCollection markerDdProperties;
        markerDdProperties.setProperty( QgsSymbolLayer::PropertyName, QgsProperty::fromExpression( spriteProperty ) );
        markerLayer->setDataDefinedProperties( markerDdProperties );

        ddLabelProperties.setProperty( QgsPalLayerSettings::ShapeSizeX, QgsProperty::fromExpression( spriteSizeProperty ) );
      }

      QgsTextBackgroundSettings backgroundSettings;
      backgroundSettings.setEnabled( true );
      backgroundSettings.setType( QgsTextBackgroundSettings::ShapeMarkerSymbol );
      backgroundSettings.setSize( spriteSize );
      backgroundSettings.setSizeUnit( context.targetUnit() );
      backgroundSettings.setSizeType( QgsTextBackgroundSettings::SizeFixed );
      backgroundSettings.setMarkerSymbol( new QgsMarkerSymbol( QgsSymbolLayerList() << markerLayer ) );
      format.setBackground( backgroundSettings );
    }
  }

  if ( textSize >= 0 )
  {
    // TODO -- this probably needs revisiting -- it was copied from the MapTiler code, but may be wrong...
    labelSettings.priority = std::min( textSize / ( context.pixelSizeConversionFactor() * 3 ), 10.0 );
  }

  labelSettings.setFormat( format );

  // use a low obstacle weight for layers by default -- we'd rather have more labels for these layers, even if placement isn't ideal
  labelSettings.obstacleSettings().setFactor( 0.1 );

  labelSettings.setDataDefinedProperties( ddLabelProperties );

  labelingStyle.setGeometryType( geometryType );
  labelingStyle.setLabelSettings( labelSettings );

  hasLabeling = true;

  hasRenderer = parseSymbolLayerAsRenderer( jsonLayer, renderer, context );
}

bool QgsMapBoxGlStyleConverter::parseSymbolLayerAsRenderer( const QVariantMap &jsonLayer, QgsVectorTileBasicRendererStyle &rendererStyle, QgsMapBoxGlStyleConversionContext &context )
{
  if ( !jsonLayer.contains( QStringLiteral( "layout" ) ) )
  {
    context.pushWarning( QObject::tr( "%1: Style layer has no layout property, skipping" ).arg( context.layerId() ) );
    return false;
  }
  const QVariantMap jsonLayout = jsonLayer.value( QStringLiteral( "layout" ) ).toMap();

  if ( jsonLayout.value( QStringLiteral( "symbol-placement" ) ).toString() == QLatin1String( "line" ) && !jsonLayout.contains( QStringLiteral( "text-field" ) ) )
  {
    QgsPropertyCollection ddProperties;

    double spacing = -1.0;
    if ( jsonLayout.contains( QStringLiteral( "symbol-spacing" ) ) )
    {
      const QVariant jsonSpacing = jsonLayout.value( QStringLiteral( "symbol-spacing" ) );
      switch ( jsonSpacing.type() )
      {
        case QVariant::Int:
        case QVariant::Double:
          spacing = jsonSpacing.toDouble() * context.pixelSizeConversionFactor();
          break;

        case QVariant::Map:
          ddProperties.setProperty( QgsSymbolLayer::PropertyInterval, parseInterpolateByZoom( jsonSpacing.toMap(), context, context.pixelSizeConversionFactor(), &spacing ) );
          break;

        case QVariant::List:
        case QVariant::StringList:
          ddProperties.setProperty( QgsSymbolLayer::PropertyInterval, parseValueList( jsonSpacing.toList(), PropertyType::Numeric, context, context.pixelSizeConversionFactor(), 255, nullptr, &spacing ) );
          break;

        default:
          context.pushWarning( QObject::tr( "%1: Skipping unsupported symbol-spacing type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonSpacing.type() ) ) );
          break;
      }
    }
    else
    {
      // defaults to 250
      spacing = 250 * context.pixelSizeConversionFactor();
    }

    bool rotateMarkers = true;
    if ( jsonLayout.contains( QStringLiteral( "icon-rotation-alignment" ) ) )
    {
      const QString alignment = jsonLayout.value( QStringLiteral( "icon-rotation-alignment" ) ).toString();
      if ( alignment == QLatin1String( "map" ) || alignment == QLatin1String( "auto" ) )
      {
        rotateMarkers = true;
      }
      else if ( alignment == QLatin1String( "viewport" ) )
      {
        rotateMarkers = false;
      }
    }

    QgsPropertyCollection markerDdProperties;
    double rotation = 0.0;
    if ( jsonLayout.contains( QStringLiteral( "icon-rotate" ) ) )
    {
      const QVariant jsonIconRotate = jsonLayout.value( QStringLiteral( "icon-rotate" ) );
      switch ( jsonIconRotate.type() )
      {
        case QVariant::Int:
        case QVariant::Double:
          rotation = jsonIconRotate.toDouble();
          break;

        case QVariant::Map:
          markerDdProperties.setProperty( QgsSymbolLayer::PropertyAngle, parseInterpolateByZoom( jsonIconRotate.toMap(), context, context.pixelSizeConversionFactor(), &rotation ) );
          break;

        case QVariant::List:
        case QVariant::StringList:
          markerDdProperties.setProperty( QgsSymbolLayer::PropertyAngle, parseValueList( jsonIconRotate.toList(), PropertyType::Numeric, context, context.pixelSizeConversionFactor(), 255, nullptr, &rotation ) );
          break;

        default:
          context.pushWarning( QObject::tr( "%1: Skipping unsupported icon-rotate type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonIconRotate.type() ) ) );
          break;
      }
    }

    QgsMarkerLineSymbolLayer *lineSymbol = new QgsMarkerLineSymbolLayer( rotateMarkers, spacing > 0 ? spacing : 1 );
    lineSymbol->setOutputUnit( context.targetUnit() );
    lineSymbol->setDataDefinedProperties( ddProperties );
    if ( spacing < 1 )
    {
      // if spacing isn't specified, it's a central point marker only
      lineSymbol->setPlacement( QgsTemplatedLineSymbolLayerBase::CentralPoint );
    }

    QgsRasterMarkerSymbolLayer *markerLayer = new QgsRasterMarkerSymbolLayer( );
    QSize spriteSize;
    QString spriteProperty, spriteSizeProperty;
    const QString sprite = retrieveSpriteAsBase64( jsonLayout.value( QStringLiteral( "icon-image" ) ), context, spriteSize, spriteProperty, spriteSizeProperty );
    if ( !sprite.isNull() )
    {
      markerLayer->setPath( sprite );
      markerLayer->setSize( spriteSize.width() );
      markerLayer->setSizeUnit( context.targetUnit() );

      if ( !spriteProperty.isEmpty() )
      {
        markerDdProperties.setProperty( QgsSymbolLayer::PropertyName, QgsProperty::fromExpression( spriteProperty ) );
        markerDdProperties.setProperty( QgsSymbolLayer::PropertyWidth, QgsProperty::fromExpression( spriteSizeProperty ) );
      }
    }

    if ( jsonLayout.contains( QStringLiteral( "icon-size" ) ) )
    {
      const QVariant jsonIconSize = jsonLayout.value( QStringLiteral( "icon-size" ) );
      double size = 1.0;
      QgsProperty property;
      switch ( jsonIconSize.type() )
      {
        case QVariant::Int:
        case QVariant::Double:
        {
          size = jsonIconSize.toDouble();
          if ( !spriteSizeProperty.isEmpty() )
          {
            markerDdProperties.setProperty( QgsSymbolLayer::PropertyWidth,
                                            QgsProperty::fromExpression( QStringLiteral( "with_variable('marker_size',%1,%2*@marker_size)" ).arg( spriteSizeProperty ).arg( size ) ) );
          }
          break;
        }

        case QVariant::Map:
          property = parseInterpolateByZoom( jsonIconSize.toMap(), context, 1, &size );
          break;

        case QVariant::List:
        case QVariant::StringList:
        default:
          context.pushWarning( QObject::tr( "%1: Skipping non-implemented icon-size type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonIconSize.type() ) ) );
          break;
      }
      markerLayer->setSize( size * spriteSize.width() );
      if ( !property.expressionString().isEmpty() )
      {
        if ( !spriteSizeProperty.isEmpty() )
        {
          markerDdProperties.setProperty( QgsSymbolLayer::PropertyWidth,
                                          QgsProperty::fromExpression( QStringLiteral( "with_variable('marker_size',%1,(%2)*@marker_size)" ).arg( spriteSizeProperty ).arg( property.expressionString() ) ) );
        }
        else
        {
          markerDdProperties.setProperty( QgsSymbolLayer::PropertyWidth,
                                          QgsProperty::fromExpression( QStringLiteral( "(%2)*%1" ).arg( spriteSize.width() ).arg( property.expressionString() ) ) );
        }
      }
    }

    markerLayer->setDataDefinedProperties( markerDdProperties );
    markerLayer->setAngle( rotation );
    lineSymbol->setSubSymbol( new QgsMarkerSymbol( QgsSymbolLayerList() << markerLayer ) );

    std::unique_ptr< QgsSymbol > symbol = std::make_unique< QgsLineSymbol >( QgsSymbolLayerList() << lineSymbol );

    // set render units
    symbol->setOutputUnit( context.targetUnit() );
    lineSymbol->setOutputUnit( context.targetUnit() );

    rendererStyle.setGeometryType( QgsWkbTypes::LineGeometry );
    rendererStyle.setSymbol( symbol.release() );
    return true;
  }
  else if ( jsonLayout.contains( QStringLiteral( "icon-image" ) ) )
  {
    const QVariantMap jsonPaint = jsonLayer.value( QStringLiteral( "paint" ) ).toMap();

    QSize spriteSize;
    QString spriteProperty, spriteSizeProperty;
    const QString sprite = retrieveSpriteAsBase64( jsonLayout.value( QStringLiteral( "icon-image" ) ), context, spriteSize, spriteProperty, spriteSizeProperty );
    if ( !sprite.isEmpty() )
    {
      QgsRasterMarkerSymbolLayer *rasterMarker = new QgsRasterMarkerSymbolLayer( );
      rasterMarker->setPath( sprite );
      rasterMarker->setSize( spriteSize.width() );
      rasterMarker->setSizeUnit( context.targetUnit() );

      QgsPropertyCollection markerDdProperties;
      if ( !spriteProperty.isEmpty() )
      {
        markerDdProperties.setProperty( QgsSymbolLayer::PropertyName, QgsProperty::fromExpression( spriteProperty ) );
        markerDdProperties.setProperty( QgsSymbolLayer::PropertyWidth, QgsProperty::fromExpression( spriteSizeProperty ) );
      }

      if ( jsonLayout.contains( QStringLiteral( "icon-size" ) ) )
      {
        const QVariant jsonIconSize = jsonLayout.value( QStringLiteral( "icon-size" ) );
        double size = 1.0;
        QgsProperty property;
        switch ( jsonIconSize.type() )
        {
          case QVariant::Int:
          case QVariant::Double:
          {
            size = jsonIconSize.toDouble();
            if ( !spriteSizeProperty.isEmpty() )
            {
              markerDdProperties.setProperty( QgsSymbolLayer::PropertyWidth,
                                              QgsProperty::fromExpression( QStringLiteral( "with_variable('marker_size',%1,%2*@marker_size)" ).arg( spriteSizeProperty ).arg( size ) ) );
            }
            break;
          }

          case QVariant::Map:
            property = parseInterpolateByZoom( jsonIconSize.toMap(), context, 1, &size );
            break;

          case QVariant::List:
          case QVariant::StringList:
          default:
            context.pushWarning( QObject::tr( "%1: Skipping non-implemented icon-size type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonIconSize.type() ) ) );
            break;
        }
        rasterMarker->setSize( size * spriteSize.width() );
        if ( !property.expressionString().isEmpty() )
        {
          if ( !spriteSizeProperty.isEmpty() )
          {
            markerDdProperties.setProperty( QgsSymbolLayer::PropertyWidth,
                                            QgsProperty::fromExpression( QStringLiteral( "with_variable('marker_size',%1,(%2)*@marker_size)" ).arg( spriteSizeProperty ).arg( property.expressionString() ) ) );
          }
          else
          {
            markerDdProperties.setProperty( QgsSymbolLayer::PropertyWidth,
                                            QgsProperty::fromExpression( QStringLiteral( "(%2)*%1" ).arg( spriteSize.width() ).arg( property.expressionString() ) ) );
          }
        }
      }

      double rotation = 0.0;
      if ( jsonLayout.contains( QStringLiteral( "icon-rotate" ) ) )
      {
        const QVariant jsonIconRotate = jsonLayout.value( QStringLiteral( "icon-rotate" ) );
        switch ( jsonIconRotate.type() )
        {
          case QVariant::Int:
          case QVariant::Double:
            rotation = jsonIconRotate.toDouble();
            break;

          case QVariant::Map:
            markerDdProperties.setProperty( QgsSymbolLayer::PropertyAngle, parseInterpolateByZoom( jsonIconRotate.toMap(), context, context.pixelSizeConversionFactor(), &rotation ) );
            break;

          case QVariant::List:
          case QVariant::StringList:
            markerDdProperties.setProperty( QgsSymbolLayer::PropertyAngle, parseValueList( jsonIconRotate.toList(), PropertyType::Numeric, context, context.pixelSizeConversionFactor(), 255, nullptr, &rotation ) );
            break;

          default:
            context.pushWarning( QObject::tr( "%1: Skipping unsupported icon-rotate type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonIconRotate.type() ) ) );
            break;
        }
      }

      double iconOpacity = -1.0;
      if ( jsonPaint.contains( QStringLiteral( "icon-opacity" ) ) )
      {
        const QVariant jsonIconOpacity = jsonPaint.value( QStringLiteral( "icon-opacity" ) );
        switch ( jsonIconOpacity.type() )
        {
          case QVariant::Int:
          case QVariant::Double:
            iconOpacity = jsonIconOpacity.toDouble();
            break;

          case QVariant::Map:
            markerDdProperties.setProperty( QgsSymbolLayer::PropertyOpacity, parseInterpolateByZoom( jsonIconOpacity.toMap(), context, 100, &iconOpacity ) );
            break;

          case QVariant::List:
          case QVariant::StringList:
            markerDdProperties.setProperty( QgsSymbolLayer::PropertyOpacity, parseValueList( jsonIconOpacity.toList(), PropertyType::Numeric, context, 100, 255, nullptr, &iconOpacity ) );
            break;

          default:
            context.pushWarning( QObject::tr( "%1: Skipping unsupported icon-opacity type (%2)" ).arg( context.layerId(), QMetaType::typeName( jsonIconOpacity.type() ) ) );
            break;
        }
      }

      rasterMarker->setDataDefinedProperties( markerDdProperties );
      rasterMarker->setAngle( rotation );
      if ( iconOpacity >= 0 )
        rasterMarker->setOpacity( iconOpacity );

      QgsMarkerSymbol *markerSymbol = new QgsMarkerSymbol( QgsSymbolLayerList() << rasterMarker );
      rendererStyle.setSymbol( markerSymbol );
      rendererStyle.setGeometryType( QgsWkbTypes::PointGeometry );
      return true;
    }
  }

  return false;
}

QgsProperty QgsMapBoxGlStyleConverter::parseInterpolateColorByZoom( const QVariantMap &json, QgsMapBoxGlStyleConversionContext &context, QColor *defaultColor )
{
  const double base = json.value( QStringLiteral( "base" ), QStringLiteral( "1" ) ).toDouble();
  const QVariantList stops = json.value( QStringLiteral( "stops" ) ).toList();
  if ( stops.empty() )
    return QgsProperty();

  QString caseString = QStringLiteral( "CASE " );

  for ( int i = 0; i < stops.length() - 1; ++i )
  {
    // step bottom zoom
    const QString bz = stops.at( i ).toList().value( 0 ).toString();
    // step top zoom
    const QString tz = stops.at( i + 1 ).toList().value( 0 ).toString();

    const QColor bottomColor = parseColor( stops.at( i ).toList().value( 1 ), context );
    const QColor topColor = parseColor( stops.at( i + 1 ).toList().value( 1 ), context );

    int bcHue;
    int bcSat;
    int bcLight;
    int bcAlpha;
    colorAsHslaComponents( bottomColor, bcHue, bcSat, bcLight, bcAlpha );
    int tcHue;
    int tcSat;
    int tcLight;
    int tcAlpha;
    colorAsHslaComponents( topColor, tcHue, tcSat, tcLight, tcAlpha );

    caseString += QStringLiteral( "WHEN @vector_tile_zoom >= %1 AND @vector_tile_zoom < %2 THEN color_hsla("
                                  "%3, %4, %5, %6) " ).arg( bz, tz,
                                      interpolateExpression( bz.toDouble(), tz.toDouble(), bcHue, tcHue, base ),
                                      interpolateExpression( bz.toDouble(), tz.toDouble(), bcSat, tcSat, base ),
                                      interpolateExpression( bz.toDouble(), tz.toDouble(), bcLight, tcLight, base ),
                                      interpolateExpression( bz.toDouble(), tz.toDouble(), bcAlpha, tcAlpha, base ) );
  }

  // top color
  const QString tz = stops.last().toList().value( 0 ).toString();
  const QColor topColor = parseColor( stops.last().toList().value( 1 ), context );
  int tcHue;
  int tcSat;
  int tcLight;
  int tcAlpha;
  colorAsHslaComponents( topColor, tcHue, tcSat, tcLight, tcAlpha );

  caseString += QStringLiteral( "WHEN @vector_tile_zoom >= %1 THEN color_hsla(%2, %3, %4, %5) "
                                "ELSE color_hsla(%2, %3, %4, %5) END" ).arg( tz )
                .arg( tcHue ).arg( tcSat ).arg( tcLight ).arg( tcAlpha );


  if ( !stops.empty() && defaultColor )
    *defaultColor = parseColor( stops.value( 0 ).toList().value( 1 ).toString(), context );

  return QgsProperty::fromExpression( caseString );
}

QgsProperty QgsMapBoxGlStyleConverter::parseInterpolateByZoom( const QVariantMap &json, QgsMapBoxGlStyleConversionContext &context, double multiplier, double *defaultNumber )
{
  const double base = json.value( QStringLiteral( "base" ), QStringLiteral( "1" ) ).toDouble();
  const QVariantList stops = json.value( QStringLiteral( "stops" ) ).toList();
  if ( stops.empty() )
    return QgsProperty();

  QString scaleExpression;
  if ( stops.size() <= 2 )
  {
    scaleExpression = interpolateExpression( stops.value( 0 ).toList().value( 0 ).toDouble(),
                      stops.last().toList().value( 0 ).toDouble(),
                      stops.value( 0 ).toList().value( 1 ).toDouble(),
                      stops.last().toList().value( 1 ).toDouble(), base, multiplier );
  }
  else
  {
    scaleExpression = parseStops( base, stops, multiplier, context );
  }

  if ( !stops.empty() && defaultNumber )
    *defaultNumber = stops.value( 0 ).toList().value( 1 ).toDouble() * multiplier;

  return QgsProperty::fromExpression( scaleExpression );
}

QgsProperty QgsMapBoxGlStyleConverter::parseInterpolateOpacityByZoom( const QVariantMap &json, int maxOpacity )
{
  const double base = json.value( QStringLiteral( "base" ), QStringLiteral( "1" ) ).toDouble();
  const QVariantList stops = json.value( QStringLiteral( "stops" ) ).toList();
  if ( stops.empty() )
    return QgsProperty();

  QString scaleExpression;
  if ( stops.length() <= 2 )
  {
    scaleExpression = QStringLiteral( "set_color_part(@symbol_color, 'alpha', %1)" )
                      .arg( interpolateExpression( stops.value( 0 ).toList().value( 0 ).toDouble(),
                            stops.last().toList().value( 0 ).toDouble(),
                            stops.value( 0 ).toList().value( 1 ).toDouble() * maxOpacity,
                            stops.last().toList().value( 1 ).toDouble() * maxOpacity, base ) );
  }
  else
  {
    scaleExpression = parseOpacityStops( base, stops, maxOpacity );
  }
  return QgsProperty::fromExpression( scaleExpression );
}

QString QgsMapBoxGlStyleConverter::parseOpacityStops( double base, const QVariantList &stops, int maxOpacity )
{
  QString caseString = QStringLiteral( "CASE WHEN @vector_tile_zoom < %1 THEN set_color_part(@symbol_color, 'alpha', %2)" )
                       .arg( stops.value( 0 ).toList().value( 0 ).toString() )
                       .arg( stops.value( 0 ).toList().value( 1 ).toDouble() * maxOpacity );

  for ( int i = 0; i < stops.size() - 1; ++i )
  {
    caseString += QStringLiteral( " WHEN @vector_tile_zoom >= %1 AND @vector_tile_zoom < %2 "
                                  "THEN set_color_part(@symbol_color, 'alpha', %3)" )
                  .arg( stops.value( i ).toList().value( 0 ).toString(),
                        stops.value( i + 1 ).toList().value( 0 ).toString(),
                        interpolateExpression( stops.value( i ).toList().value( 0 ).toDouble(),
                            stops.value( i + 1 ).toList().value( 0 ).toDouble(),
                            stops.value( i ).toList().value( 1 ).toDouble() * maxOpacity,
                            stops.value( i + 1 ).toList().value( 1 ).toDouble() * maxOpacity, base ) );
  }

  caseString += QStringLiteral( " WHEN @vector_tile_zoom >= %1 "
                                "THEN set_color_part(@symbol_color, 'alpha', %2) END" )
                .arg( stops.last().toList().value( 0 ).toString() )
                .arg( stops.last().toList().value( 1 ).toDouble() * maxOpacity );
  return caseString;
}

QgsProperty QgsMapBoxGlStyleConverter::parseInterpolatePointByZoom( const QVariantMap &json, QgsMapBoxGlStyleConversionContext &context, double multiplier, QPointF *defaultPoint )
{
  const double base = json.value( QStringLiteral( "base" ), QStringLiteral( "1" ) ).toDouble();
  const QVariantList stops = json.value( QStringLiteral( "stops" ) ).toList();
  if ( stops.empty() )
    return QgsProperty();

  QString scaleExpression;
  if ( stops.size() <= 2 )
  {
    scaleExpression = QStringLiteral( "array(%1,%2)" ).arg( interpolateExpression( stops.value( 0 ).toList().value( 0 ).toDouble(),
                      stops.last().toList().value( 0 ).toDouble(),
                      stops.value( 0 ).toList().value( 1 ).toList().value( 0 ).toDouble(),
                      stops.last().toList().value( 1 ).toList().value( 0 ).toDouble(), base, multiplier ),
                      interpolateExpression( stops.value( 0 ).toList().value( 0 ).toDouble(),
                          stops.last().toList().value( 0 ).toDouble(),
                          stops.value( 0 ).toList().value( 1 ).toList().value( 1 ).toDouble(),
                          stops.last().toList().value( 1 ).toList().value( 1 ).toDouble(), base, multiplier )
                                                          );
  }
  else
  {
    scaleExpression = parsePointStops( base, stops, context, multiplier );
  }

  if ( !stops.empty() && defaultPoint )
    *defaultPoint = QPointF( stops.value( 0 ).toList().value( 1 ).toList().value( 0 ).toDouble() * multiplier,
                             stops.value( 0 ).toList().value( 1 ).toList().value( 1 ).toDouble() * multiplier );

  return QgsProperty::fromExpression( scaleExpression );
}

QgsProperty QgsMapBoxGlStyleConverter::parseInterpolateStringByZoom( const QVariantMap &json, QgsMapBoxGlStyleConversionContext &context,
    const QVariantMap &conversionMap, QString *defaultString )
{
  const QVariantList stops = json.value( QStringLiteral( "stops" ) ).toList();
  if ( stops.empty() )
    return QgsProperty();

  const QString scaleExpression = parseStringStops( stops, context, conversionMap, defaultString );

  return QgsProperty::fromExpression( scaleExpression );
}

QString QgsMapBoxGlStyleConverter::parsePointStops( double base, const QVariantList &stops, QgsMapBoxGlStyleConversionContext &context, double multiplier )
{
  QString caseString = QStringLiteral( "CASE " );

  for ( int i = 0; i < stops.length() - 1; ++i )
  {
    // bottom zoom and value
    const QVariant bz = stops.value( i ).toList().value( 0 );
    const QVariant bv = stops.value( i ).toList().value( 1 );
    if ( bv.type() != QVariant::List && bv.type() != QVariant::StringList )
    {
      context.pushWarning( QObject::tr( "%1: Skipping unsupported offset interpolation type (%2)." ).arg( context.layerId(), QMetaType::typeName( bz.type() ) ) );
      return QString();
    }

    // top zoom and value
    const QVariant tz = stops.value( i + 1 ).toList().value( 0 );
    const QVariant tv = stops.value( i + 1 ).toList().value( 1 );
    if ( tv.type() != QVariant::List && tv.type() != QVariant::StringList )
    {
      context.pushWarning( QObject::tr( "%1: Skipping unsupported offset interpolation type (%2)." ).arg( context.layerId(), QMetaType::typeName( tz.type() ) ) );
      return QString();
    }

    caseString += QStringLiteral( "WHEN @vector_tile_zoom > %1 AND @vector_tile_zoom <= %2 "
                                  "THEN array(%3,%4)" ).arg( bz.toString(),
                                      tz.toString(),
                                      interpolateExpression( bz.toDouble(), tz.toDouble(), bv.toList().value( 0 ).toDouble(), tv.toList().value( 0 ).toDouble(), base, multiplier ),
                                      interpolateExpression( bz.toDouble(), tz.toDouble(), bv.toList().value( 1 ).toDouble(), tv.toList().value( 1 ).toDouble(), base, multiplier ) );
  }
  caseString += QLatin1String( "END" );
  return caseString;
}

QString QgsMapBoxGlStyleConverter::parseStops( double base, const QVariantList &stops, double multiplier, QgsMapBoxGlStyleConversionContext &context )
{
  QString caseString = QStringLiteral( "CASE " );

  for ( int i = 0; i < stops.length() - 1; ++i )
  {
    // bottom zoom and value
    const QVariant bz = stops.value( i ).toList().value( 0 );
    const QVariant bv = stops.value( i ).toList().value( 1 );
    if ( bz.type() == QVariant::List || bz.type() == QVariant::StringList )
    {
      context.pushWarning( QObject::tr( "%1: Expressions in interpolation function are not supported, skipping." ).arg( context.layerId() ) );
      return QString();
    }

    // top zoom and value
    const QVariant tz = stops.value( i + 1 ).toList().value( 0 );
    const QVariant tv = stops.value( i + 1 ).toList().value( 1 );
    if ( tz.type() == QVariant::List || tz.type() == QVariant::StringList )
    {
      context.pushWarning( QObject::tr( "%1: Expressions in interpolation function are not supported, skipping." ).arg( context.layerId() ) );
      return QString();
    }

    caseString += QStringLiteral( "WHEN @vector_tile_zoom > %1 AND @vector_tile_zoom <= %2 "
                                  "THEN %3 " ).arg( bz.toString(),
                                      tz.toString(),
                                      interpolateExpression( bz.toDouble(), tz.toDouble(), bv.toDouble(), tv.toDouble(), base, multiplier ) );
  }

  const QVariant z = stops.last().toList().value( 0 );
  const QVariant v = stops.last().toList().value( 1 );
  caseString += QStringLiteral( "WHEN @vector_tile_zoom > %1 "
                                "THEN %2 END" ).arg( z.toString() ).arg( v.toDouble() * multiplier );
  return caseString;
}

QString QgsMapBoxGlStyleConverter::parseStringStops( const QVariantList &stops, QgsMapBoxGlStyleConversionContext &context, const QVariantMap &conversionMap, QString *defaultString )
{
  QString caseString = QStringLiteral( "CASE " );

  for ( int i = 0; i < stops.length() - 1; ++i )
  {
    // bottom zoom and value
    const QVariant bz = stops.value( i ).toList().value( 0 );
    const QString bv = stops.value( i ).toList().value( 1 ).toString();
    if ( bz.type() == QVariant::List || bz.type() == QVariant::StringList )
    {
      context.pushWarning( QObject::tr( "%1: Expressions in interpolation function are not supported, skipping." ).arg( context.layerId() ) );
      return QString();
    }

    // top zoom
    const QVariant tz = stops.value( i + 1 ).toList().value( 0 );
    if ( tz.type() == QVariant::List || tz.type() == QVariant::StringList )
    {
      context.pushWarning( QObject::tr( "%1: Expressions in interpolation function are not supported, skipping." ).arg( context.layerId() ) );
      return QString();
    }

    caseString += QStringLiteral( "WHEN @vector_tile_zoom > %1 AND @vector_tile_zoom <= %2 "
                                  "THEN %3 " ).arg( bz.toString(),
                                      tz.toString(),
                                      QgsExpression::quotedValue( conversionMap.value( bv, bv ) ) );
  }
  caseString += QStringLiteral( "ELSE %1 END" ).arg( QgsExpression::quotedValue( conversionMap.value( stops.constLast().toList().value( 1 ).toString(),
                stops.constLast().toList().value( 1 ) ) ) );
  if ( defaultString )
    *defaultString = stops.constLast().toList().value( 1 ).toString();
  return caseString;
}

QgsProperty QgsMapBoxGlStyleConverter::parseValueList( const QVariantList &json, QgsMapBoxGlStyleConverter::PropertyType type, QgsMapBoxGlStyleConversionContext &context, double multiplier, int maxOpacity, QColor *defaultColor, double *defaultNumber )
{
  const QString method = json.value( 0 ).toString();
  if ( method == QLatin1String( "interpolate" ) )
  {
    return parseInterpolateListByZoom( json, type, context, multiplier, maxOpacity, defaultColor, defaultNumber );
  }
  else if ( method == QLatin1String( "match" ) )
  {
    return parseMatchList( json, type, context, multiplier, maxOpacity, defaultColor, defaultNumber );
  }
  else
  {
    context.pushWarning( QObject::tr( "%1: Could not interpret value list with method %2" ).arg( context.layerId(), method ) );
    return QgsProperty();
  }
}

QgsProperty QgsMapBoxGlStyleConverter::parseMatchList( const QVariantList &json, QgsMapBoxGlStyleConverter::PropertyType type, QgsMapBoxGlStyleConversionContext &context, double multiplier, int maxOpacity, QColor *defaultColor, double *defaultNumber )
{
  const QString attribute = parseExpression( json.value( 1 ).toList(), context );
  if ( attribute.isEmpty() )
  {
    context.pushWarning( QObject::tr( "%1: Could not interpret match list" ).arg( context.layerId() ) );
    return QgsProperty();
  }

  QString caseString = QStringLiteral( "CASE " );

  for ( int i = 2; i < json.length() - 1; i += 2 )
  {
    const QVariantList keys = json.value( i ).toList();

    QStringList matchString;
    for ( const QVariant &key : keys )
    {
      matchString << QgsExpression::quotedValue( key );
    }

    const QVariant value = json.value( i + 1 );

    QString valueString;
    switch ( type )
    {
      case Color:
      {
        const QColor color = parseColor( value, context );
        valueString = QgsExpression::quotedString( color.name() );
        break;
      }

      case Numeric:
      {
        const double v = value.toDouble() * multiplier;
        valueString = QString::number( v );
        break;
      }

      case Opacity:
      {
        const double v = value.toDouble() * maxOpacity;
        valueString = QString::number( v );
        break;
      }

      case Point:
      {
        valueString = QStringLiteral( "array(%1,%2)" ).arg( value.toList().value( 0 ).toDouble() * multiplier,
                      value.toList().value( 0 ).toDouble() * multiplier );
        break;
      }

    }

    caseString += QStringLiteral( "WHEN %1 IN (%2) THEN %3 " ).arg( attribute,
                  matchString.join( ',' ), valueString );
  }


  QString elseValue;
  switch ( type )
  {
    case Color:
    {
      const QColor color = parseColor( json.constLast(), context );
      if ( defaultColor )
        *defaultColor = color;

      elseValue = QgsExpression::quotedString( color.name() );
      break;
    }

    case Numeric:
    {
      const double v = json.constLast().toDouble() * multiplier;
      if ( defaultNumber )
        *defaultNumber = v;
      elseValue = QString::number( v );
      break;
    }

    case Opacity:
    {
      const double v = json.constLast().toDouble() * maxOpacity;
      if ( defaultNumber )
        *defaultNumber = v;
      elseValue = QString::number( v );
      break;
    }

    case Point:
    {
      elseValue = QStringLiteral( "array(%1,%2)" ).arg( json.constLast().toList().value( 0 ).toDouble() * multiplier,
                  json.constLast().toList().value( 0 ).toDouble() * multiplier );
      break;
    }

  }

  caseString += QStringLiteral( "ELSE %1 END" ).arg( elseValue );
  return QgsProperty::fromExpression( caseString );
}

QgsProperty QgsMapBoxGlStyleConverter::parseInterpolateListByZoom( const QVariantList &json, PropertyType type, QgsMapBoxGlStyleConversionContext &context, double multiplier, int maxOpacity, QColor *defaultColor, double *defaultNumber )
{
  if ( json.value( 0 ).toString() != QLatin1String( "interpolate" ) )
  {
    context.pushWarning( QObject::tr( "%1: Could not interpret value list" ).arg( context.layerId() ) );
    return QgsProperty();
  }

  double base = 1;
  const QString technique = json.value( 1 ).toList().value( 0 ).toString();
  if ( technique == QLatin1String( "linear" ) )
    base = 1;
  else if ( technique == QLatin1String( "exponential" ) )
    base = json.value( 1 ).toList(). value( 1 ).toDouble();
  else if ( technique == QLatin1String( "cubic-bezier" ) )
  {
    context.pushWarning( QObject::tr( "%1: Cubic-bezier interpolation is not supported, linear used instead." ).arg( context.layerId() ) );
    base = 1;
  }
  else
  {
    context.pushWarning( QObject::tr( "%1: Skipping not implemented interpolation method %2" ).arg( context.layerId(), technique ) );
    return QgsProperty();
  }

  if ( json.value( 2 ).toList().value( 0 ).toString() != QLatin1String( "zoom" ) )
  {
    context.pushWarning( QObject::tr( "%1: Skipping not implemented interpolation input %2" ).arg( context.layerId(), json.value( 2 ).toString() ) );
    return QgsProperty();
  }

  //  Convert stops into list of lists
  QVariantList stops;
  for ( int i = 3; i < json.length(); i += 2 )
  {
    stops.push_back( QVariantList() << json.value( i ).toString() << json.value( i + 1 ).toString() );
  }

  QVariantMap props;
  props.insert( QStringLiteral( "stops" ), stops );
  props.insert( QStringLiteral( "base" ), base );
  switch ( type )
  {
    case PropertyType::Color:
      return parseInterpolateColorByZoom( props, context, defaultColor );

    case PropertyType::Numeric:
      return parseInterpolateByZoom( props, context, multiplier, defaultNumber );

    case PropertyType::Opacity:
      return parseInterpolateOpacityByZoom( props, maxOpacity );

    case PropertyType::Point:
      return parseInterpolatePointByZoom( props, context, multiplier );
  }
  return QgsProperty();
}

QColor QgsMapBoxGlStyleConverter::parseColor( const QVariant &color, QgsMapBoxGlStyleConversionContext &context )
{
  if ( color.type() != QVariant::String )
  {
    context.pushWarning( QObject::tr( "%1: Could not parse non-string color %2, skipping" ).arg( context.layerId(), color.toString() ) );
    return QColor();
  }

  return QgsSymbolLayerUtils::parseColor( color.toString() );
}

void QgsMapBoxGlStyleConverter::colorAsHslaComponents( const QColor &color, int &hue, int &saturation, int &lightness, int &alpha )
{
  hue = std::max( 0, color.hslHue() );
  saturation = color.hslSaturation() / 255.0 * 100;
  lightness = color.lightness() / 255.0 * 100;
  alpha = color.alpha();
}

QString QgsMapBoxGlStyleConverter::interpolateExpression( double zoomMin, double zoomMax, double valueMin, double valueMax, double base, double multiplier )
{
  // special case!
  if ( qgsDoubleNear( valueMin, valueMax ) )
    return QString::number( valueMin * multiplier );

  QString expression;
  if ( base == 1 )
  {
    expression = QStringLiteral( "scale_linear(@vector_tile_zoom,%1,%2,%3,%4)" ).arg( zoomMin )
                 .arg( zoomMax )
                 .arg( valueMin )
                 .arg( valueMax );
  }
  else
  {
    expression = QStringLiteral( "scale_exp(@vector_tile_zoom,%1,%2,%3,%4,%5)" ).arg( zoomMin )
                 .arg( zoomMax )
                 .arg( valueMin )
                 .arg( valueMax )
                 .arg( base );
  }

  if ( multiplier != 1 )
    return QStringLiteral( "%1 * %2" ).arg( expression ).arg( multiplier );
  else
    return expression;
}

Qt::PenCapStyle QgsMapBoxGlStyleConverter::parseCapStyle( const QString &style )
{
  if ( style == QLatin1String( "round" ) )
    return Qt::RoundCap;
  else if ( style == QLatin1String( "square" ) )
    return Qt::SquareCap;
  else
    return Qt::FlatCap; // "butt" is default
}

Qt::PenJoinStyle QgsMapBoxGlStyleConverter::parseJoinStyle( const QString &style )
{
  if ( style == QLatin1String( "bevel" ) )
    return Qt::BevelJoin;
  else if ( style == QLatin1String( "round" ) )
    return Qt::RoundJoin;
  else
    return Qt::MiterJoin; // "miter" is default
}

QString QgsMapBoxGlStyleConverter::parseExpression( const QVariantList &expression, QgsMapBoxGlStyleConversionContext &context )
{
  QString op = expression.value( 0 ).toString();
  if ( op == QLatin1String( "all" )
       || op == QLatin1String( "any" )
       || op == QLatin1String( "none" ) )
  {
    QStringList parts;
    for ( int i = 1; i < expression.size(); ++i )
    {
      const QString part = parseValue( expression.at( i ), context );
      if ( part.isEmpty() )
      {
        context.pushWarning( QObject::tr( "%1: Skipping unsupported expression" ).arg( context.layerId() ) );
        return QString();
      }
      parts << part;
    }

    if ( op == QLatin1String( "none" ) )
      return QStringLiteral( "NOT (%1)" ).arg( parts.join( QLatin1String( ") AND NOT (" ) ) );

    QString operatorString;
    if ( op == QLatin1String( "all" ) )
      operatorString = QStringLiteral( ") AND (" );
    else if ( op == QLatin1String( "any" ) )
      operatorString = QStringLiteral( ") OR (" );

    return QStringLiteral( "(%1)" ).arg( parts.join( operatorString ) );
  }
  else if ( op == '!' )
  {
    // ! inverts next expression's meaning
    QVariantList contraJsonExpr = expression.value( 1 ).toList();
    contraJsonExpr[0] = QString( op + contraJsonExpr[0].toString() );
    // ['!', ['has', 'level']] -> ['!has', 'level']
    return parseKey( contraJsonExpr );
  }
  else if ( op == QLatin1String( "==" )
            || op == QLatin1String( "!=" )
            || op == QLatin1String( ">=" )
            || op == '>'
            || op == QLatin1String( "<=" )
            || op == '<' )
  {
    // use IS and NOT IS instead of = and != because they can deal with NULL values
    if ( op == QLatin1String( "==" ) )
      op = QStringLiteral( "IS" );
    else if ( op == QLatin1String( "!=" ) )
      op = QStringLiteral( "IS NOT" );
    return QStringLiteral( "%1 %2 %3" ).arg( parseKey( expression.value( 1 ) ),
           op, parseValue( expression.value( 2 ), context ) );
  }
  else if ( op == QLatin1String( "has" ) )
  {
    return parseKey( expression.value( 1 ) ) + QStringLiteral( " IS NOT NULL" );
  }
  else if ( op == QLatin1String( "!has" ) )
  {
    return parseKey( expression.value( 1 ) ) + QStringLiteral( " IS NULL" );
  }
  else if ( op == QLatin1String( "in" ) || op == QLatin1String( "!in" ) )
  {
    const QString key = parseKey( expression.value( 1 ) );
    QStringList parts;
    for ( int i = 2; i < expression.size(); ++i )
    {
      const QString part = parseValue( expression.at( i ), context );
      if ( part.isEmpty() )
      {
        context.pushWarning( QObject::tr( "%1: Skipping unsupported expression" ).arg( context.layerId() ) );
        return QString();
      }
      parts << part;
    }
    if ( op == QLatin1String( "in" ) )
      return QStringLiteral( "%1 IN (%2)" ).arg( key, parts.join( QLatin1String( ", " ) ) );
    else
      return QStringLiteral( "(%1 IS NULL OR %1 NOT IN (%2))" ).arg( key, parts.join( QLatin1String( ", " ) ) );
  }
  else if ( op == QLatin1String( "get" ) )
  {
    return parseKey( expression.value( 1 ) );
  }
  else if ( op == QLatin1String( "match" ) )
  {
    const QString attribute = expression.value( 1 ).toList().value( 1 ).toString();

    if ( expression.size() == 5
         && expression.at( 3 ).type() == QVariant::Bool && expression.at( 3 ).toBool() == true
         && expression.at( 4 ).type() == QVariant::Bool && expression.at( 4 ).toBool() == false )
    {
      // simple case, make a nice simple expression instead of a CASE statement
      if ( expression.at( 2 ).type() == QVariant::List || expression.at( 2 ).type() == QVariant::StringList )
      {
        QStringList parts;
        for ( const QVariant &p : expression.at( 2 ).toList() )
        {
          parts << QgsExpression::quotedValue( p );
        }

        if ( parts.size() > 1 )
          return QStringLiteral( "%1 IN (%2)" ).arg( QgsExpression::quotedColumnRef( attribute ), parts.join( ", " ) );
        else
          return QgsExpression::createFieldEqualityExpression( attribute, expression.at( 2 ).toList().value( 0 ) );
      }
      else if ( expression.at( 2 ).type() == QVariant::String || expression.at( 2 ).type() == QVariant::Int
                || expression.at( 2 ).type() == QVariant::Double )
      {
        return QgsExpression::createFieldEqualityExpression( attribute, expression.at( 2 ) );
      }
      else
      {
        context.pushWarning( QObject::tr( "%1: Skipping unsupported expression" ).arg( context.layerId() ) );
        return QString();
      }
    }
    else
    {
      QString caseString = QStringLiteral( "CASE " );
      for ( int i = 2; i < expression.size() - 2; i += 2 )
      {
        if ( expression.at( i ).type() == QVariant::List || expression.at( i ).type() == QVariant::StringList )
        {
          QStringList parts;
          for ( const QVariant &p : expression.at( i ).toList() )
          {
            parts << QgsExpression::quotedValue( p );
          }

          if ( parts.size() > 1 )
            caseString += QStringLiteral( "WHEN %1 IN (%2) " ).arg( QgsExpression::quotedColumnRef( attribute ), parts.join( ", " ) );
          else
            caseString += QStringLiteral( "WHEN %1 " ).arg( QgsExpression::createFieldEqualityExpression( attribute, expression.at( i ).toList().value( 0 ) ) );
        }
        else if ( expression.at( i ).type() == QVariant::String || expression.at( i ).type() == QVariant::Int
                  || expression.at( i ).type() == QVariant::Double )
        {
          caseString += QStringLiteral( "WHEN (%1) " ).arg( QgsExpression::createFieldEqualityExpression( attribute, expression.at( i ) ) );
        }

        caseString += QStringLiteral( "THEN %1 " ).arg( QgsExpression::quotedValue( expression.at( i + 1 ) ) );
      }
      caseString += QStringLiteral( "ELSE %1 END" ).arg( QgsExpression::quotedValue( expression.last() ) );
      return caseString;
    }
  }
  else if ( op == QLatin1String( "to-string" ) )
  {
    return QStringLiteral( "to_string(%1)" ).arg( parseExpression( expression.value( 1 ).toList(), context ) );
  }
  else
  {
    context.pushWarning( QObject::tr( "%1: Skipping unsupported expression" ).arg( context.layerId() ) );
    return QString();
  }
}

QImage QgsMapBoxGlStyleConverter::retrieveSprite( const QString &name, QgsMapBoxGlStyleConversionContext &context, QSize &spriteSize )
{
  if ( context.spriteImage().isNull() )
  {
    context.pushWarning( QObject::tr( "%1: Could not retrieve sprite '%2'" ).arg( context.layerId(), name ) );
    return QImage();
  }

  const QVariantMap spriteDefinition = context.spriteDefinitions().value( name ).toMap();
  if ( spriteDefinition.size() == 0 )
  {
    context.pushWarning( QObject::tr( "%1: Could not retrieve sprite '%2'" ).arg( context.layerId(), name ) );
    return QImage();
  }

  const QImage sprite = context.spriteImage().copy( spriteDefinition.value( QStringLiteral( "x" ) ).toInt(),
                        spriteDefinition.value( QStringLiteral( "y" ) ).toInt(),
                        spriteDefinition.value( QStringLiteral( "width" ) ).toInt(),
                        spriteDefinition.value( QStringLiteral( "height" ) ).toInt() );
  if ( sprite.isNull() )
  {
    context.pushWarning( QObject::tr( "%1: Could not retrieve sprite '%2'" ).arg( context.layerId(), name ) );
    return QImage();
  }

  spriteSize = sprite.size() / spriteDefinition.value( QStringLiteral( "pixelRatio" ) ).toDouble() * context.pixelSizeConversionFactor();
  return sprite;
}

QString QgsMapBoxGlStyleConverter::retrieveSpriteAsBase64( const QVariant &value, QgsMapBoxGlStyleConversionContext &context, QSize &spriteSize, QString &spriteProperty, QString &spriteSizeProperty )
{
  QString spritePath;

  auto prepareBase64 = []( const QImage & sprite )
  {
    QString path;
    if ( !sprite.isNull() )
    {
      QByteArray blob;
      QBuffer buffer( &blob );
      buffer.open( QIODevice::WriteOnly );
      sprite.save( &buffer, "PNG" );
      buffer.close();
      const QByteArray encoded = blob.toBase64();
      path = QString( encoded );
      path.prepend( QLatin1String( "base64:" ) );
    }
    return path;
  };

  switch ( value.type() )
  {
    case QVariant::String:
    {
      QString spriteName = value.toString();
      const QRegularExpression fieldNameMatch( QStringLiteral( "{([^}]+)}" ) );
      QRegularExpressionMatch match = fieldNameMatch.match( spriteName );
      if ( match.hasMatch() )
      {
        const QString fieldName = match.captured( 1 );
        spriteProperty = QStringLiteral( "CASE" );
        spriteSizeProperty = QStringLiteral( "CASE" );

        spriteName.replace( "(", QLatin1String( "\\(" ) );
        spriteName.replace( ")", QLatin1String( "\\)" ) );
        spriteName.replace( fieldNameMatch, QStringLiteral( "([^\\/\\\\]+)" ) );
        const QRegularExpression fieldValueMatch( spriteName );
        const QStringList spriteNames = context.spriteDefinitions().keys();
        for ( const QString &name : spriteNames )
        {
          match = fieldValueMatch.match( name );
          if ( match.hasMatch() )
          {
            QSize size;
            QString path;
            const QString fieldValue = match.captured( 1 );
            const QImage sprite = retrieveSprite( name, context, size );
            path = prepareBase64( sprite );
            if ( spritePath.isEmpty() && !path.isEmpty() )
            {
              spritePath = path;
              spriteSize = size;
            }

            spriteProperty += QStringLiteral( " WHEN \"%1\" = '%2' THEN '%3'" )
                              .arg( fieldName, fieldValue, path );
            spriteSizeProperty += QStringLiteral( " WHEN \"%1\" = '%2' THEN %3" )
                                  .arg( fieldName ).arg( fieldValue ).arg( size.width() );
          }
        }

        spriteProperty += QLatin1String( " END" );
        spriteSizeProperty += QLatin1String( " END" );
      }
      else
      {
        spriteProperty.clear();
        spriteSizeProperty.clear();
        const QImage sprite = retrieveSprite( spriteName, context, spriteSize );
        spritePath = prepareBase64( sprite );
      }
      break;
    }

    case QVariant::Map:
    {
      const QVariantList stops = value.toMap().value( QStringLiteral( "stops" ) ).toList();
      if ( stops.size() == 0 )
        break;

      QString path;
      QSize size;
      QImage sprite;

      sprite = retrieveSprite( stops.value( 0 ).toList().value( 1 ).toString(), context, spriteSize );
      spritePath = prepareBase64( sprite );

      spriteProperty = QStringLiteral( "CASE WHEN @vector_tile_zoom < %1 THEN '%2'" )
                       .arg( stops.value( 0 ).toList().value( 0 ).toString() )
                       .arg( spritePath );
      spriteSizeProperty = QStringLiteral( "CASE WHEN @vector_tile_zoom < %1 THEN %2" )
                           .arg( stops.value( 0 ).toList().value( 0 ).toString() )
                           .arg( spriteSize.width() );

      for ( int i = 0; i < stops.size() - 1; ++i )
      {
        ;
        sprite = retrieveSprite( stops.value( 0 ).toList().value( 1 ).toString(), context, size );
        path = prepareBase64( sprite );

        spriteProperty += QStringLiteral( " WHEN @vector_tile_zoom >= %1 AND @vector_tile_zoom < %2 "
                                          "THEN '%3'" )
                          .arg( stops.value( i ).toList().value( 0 ).toString(),
                                stops.value( i + 1 ).toList().value( 0 ).toString(),
                                path );
        spriteSizeProperty += QStringLiteral( " WHEN @vector_tile_zoom >= %1 AND @vector_tile_zoom < %2 "
                                              "THEN %3" )
                              .arg( stops.value( i ).toList().value( 0 ).toString(),
                                    stops.value( i + 1 ).toList().value( 0 ).toString() )
                              .arg( size.width() );
      }
      sprite = retrieveSprite( stops.last().toList().value( 1 ).toString(), context, size );
      path = prepareBase64( sprite );

      spriteProperty += QStringLiteral( " WHEN @vector_tile_zoom >= %1 "
                                        "THEN '%2' END" )
                        .arg( stops.last().toList().value( 0 ).toString() )
                        .arg( path );
      spriteSizeProperty += QStringLiteral( " WHEN @vector_tile_zoom >= %1 "
                                            "THEN %2 END" )
                            .arg( stops.last().toList().value( 0 ).toString() )
                            .arg( size.width() );
      break;
    }

    case QVariant::List:
    {
      const QVariantList json = value.toList();
      const QString method = json.value( 0 ).toString();
      if ( method != QLatin1String( "match" ) )
      {
        context.pushWarning( QObject::tr( "%1: Could not interpret sprite value list with method %2" ).arg( context.layerId(), method ) );
        break;
      }

      const QString attribute = parseExpression( json.value( 1 ).toList(), context );
      if ( attribute.isEmpty() )
      {
        context.pushWarning( QObject::tr( "%1: Could not interpret match list" ).arg( context.layerId() ) );
        break;
      }

      spriteProperty = QStringLiteral( "CASE " );
      spriteSizeProperty = QStringLiteral( "CASE " );

      for ( int i = 2; i < json.length() - 1; i += 2 )
      {
        const QVariantList keys = json.value( i ).toList();

        QStringList matchString;
        for ( const QVariant &key : keys )
        {
          matchString << QgsExpression::quotedValue( key );
        }

        const QVariant value = json.value( i + 1 );

        const QImage sprite = retrieveSprite( value.toString(), context, spriteSize );
        spritePath = prepareBase64( sprite );

        spriteProperty += QStringLiteral( " WHEN %1 IN (%2) "
                                          "THEN '%3' " ).arg( attribute,
                                              matchString.join( ',' ),
                                              spritePath );

        spriteSizeProperty += QStringLiteral( " WHEN %1 IN (%2) "
                                              "THEN %3 " ).arg( attribute,
                                                  matchString.join( ',' ) ).arg( spriteSize.width() );
      }

      const QImage sprite = retrieveSprite( json.constLast().toString(), context, spriteSize );
      spritePath = prepareBase64( sprite );

      spriteProperty += QStringLiteral( "ELSE %1 END" ).arg( spritePath );
      spriteSizeProperty += QStringLiteral( "ELSE %3 END" ).arg( spriteSize.width() );
      break;
    }

    default:
      context.pushWarning( QObject::tr( "%1: Skipping unsupported sprite type (%2)." ).arg( context.layerId(), QMetaType::typeName( value.type() ) ) );
      break;
  }

  return spritePath;
}

QString QgsMapBoxGlStyleConverter::parseValue( const QVariant &value, QgsMapBoxGlStyleConversionContext &context )
{
  switch ( value.type() )
  {
    case QVariant::List:
    case QVariant::StringList:
      return parseExpression( value.toList(), context );

    case QVariant::String:
      return QgsExpression::quotedValue( value.toString() );

    case QVariant::Int:
    case QVariant::Double:
      return value.toString();

    default:
      context.pushWarning( QObject::tr( "%1: Skipping unsupported expression part" ).arg( context.layerId() ) );
      break;
  }
  return QString();
}

QString QgsMapBoxGlStyleConverter::parseKey( const QVariant &value )
{
  if ( value.toString() == QLatin1String( "$type" ) )
    return QStringLiteral( "_geom_type" );
  else if ( value.type() == QVariant::List || value.type() == QVariant::StringList )
  {
    if ( value.toList().size() > 1 )
      return value.toList().at( 1 ).toString();
    else
      return value.toList().value( 0 ).toString();
  }
  return QgsExpression::quotedColumnRef( value.toString() );
}

QgsVectorTileRenderer *QgsMapBoxGlStyleConverter::renderer() const
{
  return mRenderer ? mRenderer->clone() : nullptr;
}

QgsVectorTileLabeling *QgsMapBoxGlStyleConverter::labeling() const
{
  return mLabeling ? mLabeling->clone() : nullptr;
}

//
// QgsMapBoxGlStyleConversionContext
//
void QgsMapBoxGlStyleConversionContext::pushWarning( const QString &warning )
{
  QgsDebugMsg( warning );
  mWarnings << warning;
}

QgsUnitTypes::RenderUnit QgsMapBoxGlStyleConversionContext::targetUnit() const
{
  return mTargetUnit;
}

void QgsMapBoxGlStyleConversionContext::setTargetUnit( QgsUnitTypes::RenderUnit targetUnit )
{
  mTargetUnit = targetUnit;
}

double QgsMapBoxGlStyleConversionContext::pixelSizeConversionFactor() const
{
  return mSizeConversionFactor;
}

void QgsMapBoxGlStyleConversionContext::setPixelSizeConversionFactor( double sizeConversionFactor )
{
  mSizeConversionFactor = sizeConversionFactor;
}

QImage QgsMapBoxGlStyleConversionContext::spriteImage() const
{
  return mSpriteImage;
}

QVariantMap QgsMapBoxGlStyleConversionContext::spriteDefinitions() const
{
  return mSpriteDefinitions;
}

void QgsMapBoxGlStyleConversionContext::setSprites( const QImage &image, const QVariantMap &definitions )
{
  mSpriteImage = image;
  mSpriteDefinitions = definitions;
}

void QgsMapBoxGlStyleConversionContext::setSprites( const QImage &image, const QString &definitions )
{
  setSprites( image, QgsJsonUtils::parseJson( definitions ).toMap() );
}

QString QgsMapBoxGlStyleConversionContext::layerId() const
{
  return mLayerId;
}

void QgsMapBoxGlStyleConversionContext::setLayerId( const QString &value )
{
  mLayerId = value;
}
