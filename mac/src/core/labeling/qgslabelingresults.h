/***************************************************************************
  qgslabelingresults.h
  -------------------
   begin                : February 2021
   copyright            : (C) Nyall Dawson
   email                : nyall dot dawson at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSLABELINGRESULTS_H
#define QGSLABELINGRESULTS_H

#include "qgis_core.h"
#include "qgis_sip.h"
#include "qgslabelposition.h"
#include "qgscalloutposition.h"
#include <memory>

class QgsLabelSearchTree;

/**
 * \ingroup core
 * \brief Class that stores computed placement from labeling engine.
 * \since QGIS 2.4
 */
class CORE_EXPORT QgsLabelingResults
{
  public:
    QgsLabelingResults();
    ~QgsLabelingResults();

    //! QgsLabelingResults cannot be copied.
    QgsLabelingResults( const QgsLabelingResults & ) = delete;
    //! QgsLabelingResults cannot be copied.
    QgsLabelingResults &operator=( const QgsLabelingResults &rh ) = delete;

    /**
     * Returns a list of all labels generated by the labeling run.
     *
     * \since QGIS 3.20
     */
    QList< QgsLabelPosition > allLabels() const;

    /**
     * Returns the details of any labels placed at the specified point (in map coordinates).
     */
    QList<QgsLabelPosition> labelsAtPosition( const QgsPointXY &p ) const;

    /**
     * Returns the details of any labels placed within the specified rectangle (in map coordinates).
     */
    QList<QgsLabelPosition> labelsWithinRect( const QgsRectangle &r ) const;

    /**
     * Returns a list of callouts with origins or destinations inside the given \a rectangle.
     *
     * The \a rectangle is specified in map coordinates.
     *
     * \since QGIS 3.20
     */
    QList<QgsCalloutPosition> calloutsWithinRectangle( const QgsRectangle &rectangle ) const;

    /**
     * Sets the map \a settings associated with the labeling run.
     * \since QGIS 3.4.8
     */
    void setMapSettings( const QgsMapSettings &settings );

  private:
#ifdef SIP_RUN
    QgsLabelingResults( const QgsLabelingResults & );
#endif

    std::unique_ptr< QgsLabelSearchTree > mLabelSearchTree;

    friend class QgsPalLabeling;
    friend class QgsVectorLayerLabelProvider;
    friend class QgsVectorLayerDiagramProvider;
};

#endif // QGSLABELINGRESULTS_H
