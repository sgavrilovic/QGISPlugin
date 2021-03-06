/**********************************************************************
 *
 * GEOS - Geometry Engine Open Source
 * http://geos.osgeo.org
 *
 * Copyright (C) 2005-2006 Refractions Research Inc.
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of the GNU Lesser General Public Licence as published
 * by the Free Software Foundation.
 * See the COPYING file for more information.
 *
 **********************************************************************
 *
 * Last port: geom/Envelope.java rev 1.46 (JTS-1.10)
 *
 **********************************************************************/

#ifndef GEOS_GEOM_ENVELOPE_INL
#define GEOS_GEOM_ENVELOPE_INL

#include <algorithm> // std::min, std::max
#include <cassert>
#include <numeric> // std::signbit
#include <geos/geom/Coordinate.h>
#include <geos/geom/Envelope.h>

#if GEOS_DEBUG
# include <iostream>
#endif

namespace geos {
namespace geom { // geos::geom

/*public*/
INLINE
Envelope::Envelope() :
    minx(DoubleNotANumber),
    maxx(DoubleNotANumber),
    miny(DoubleNotANumber),
    maxy(DoubleNotANumber)
{}

/*public*/
INLINE
Envelope::Envelope(double x1, double x2, double y1, double y2)
{
    init(x1, x2, y1, y2);
}
/*public*/
INLINE
Envelope::Envelope(const Coordinate& p1, const Coordinate& p2)
{
    init(p1, p2);
}

/*public*/
INLINE
Envelope::Envelope(const Coordinate& p)
{
    init(p);
}

/*public*/
INLINE double
Envelope::distance(double x0, double y0, double x1, double y1)
{
    double dx = x1 - x0;
    double dy = y1 - y0;
    return std::sqrt(dx * dx + dy * dy);
}

/*public*/
INLINE void
Envelope::expandToInclude(const Coordinate& p)
{
    expandToInclude(p.x, p.y);
}

INLINE void
Envelope::expandToInclude(const Envelope& other)
{
    return expandToInclude(&other);
}

/*public*/
INLINE void
Envelope::expandToInclude(const Envelope* other)
{
    if(isNull()) {
        minx = other->minx;
        maxx = other->maxx;
        miny = other->miny;
        maxy = other->maxy;
    }
    else {
        if(other->minx < minx) {
            minx = other->minx;
        }
        if(other->maxx > maxx) {
            maxx = other->maxx;
        }
        if(other->miny < miny) {
            miny = other->miny;
        }
        if(other->maxy > maxy) {
            maxy = other->maxy;
        }
    }
}

/*public*/
INLINE void
Envelope::expandToInclude(double x, double y)
{
    if(isNull()) {
        minx = x;
        maxx = x;
        miny = y;
        maxy = y;
    }
    else {
        if(x < minx) {
            minx = x;
        }
        if(x > maxx) {
            maxx = x;
        }
        if(y < miny) {
            miny = y;
        }
        if(y > maxy) {
            maxy = y;
        }
    }
}

/*public*/
INLINE double
Envelope::getMaxY() const
{
    assert(!isNull());
    return maxy;
}

/*public*/
INLINE double
Envelope::getMaxX() const
{
    assert(!isNull());
    return maxx;
}

/*public*/
INLINE double
Envelope::getMinY() const
{
    assert(!isNull());
    return miny;
}

/*public*/
INLINE double
Envelope::getMinX() const
{
    assert(!isNull());
    return minx;
}

/*public*/
INLINE double
Envelope::getWidth() const
{
    if(isNull()) {
        return 0;
    }
    return maxx - minx;
}

/*public*/
INLINE double
Envelope::getHeight() const
{
    if(isNull()) {
        return 0;
    }
    return maxy - miny;
}

/*public*/
INLINE void
Envelope::init()
{
    setToNull();
}

/*public*/
INLINE void
Envelope::init(double x1, double x2, double y1, double y2)
{
    if(x1 < x2) {
        minx = x1;
        maxx = x2;
    }
    else {
        minx = x2;
        maxx = x1;
    }
    if(y1 < y2) {
        miny = y1;
        maxy = y2;
    }
    else {
        miny = y2;
        maxy = y1;
    }
}

/*public*/
INLINE void
Envelope::init(const Coordinate& p1, const Coordinate& p2)
{
    init(p1.x, p2.x, p1.y, p2.y);
}

/*public*/
INLINE void
Envelope::init(const Coordinate& p)
{
    init(p.x, p.x, p.y, p.y);
}

/*public*/
INLINE bool
Envelope::intersects(const Coordinate& other) const
{
    return (other.x <= maxx && other.x >= minx &&
            other.y <= maxy && other.y >= miny);
}

/*public*/
INLINE bool
Envelope::intersects(const Envelope& other) const
{
    return intersects(&other);
}

/*public*/
INLINE bool
Envelope::isNull(void) const
{
    return std::isnan(maxx);
}

/*public*/
INLINE bool
Envelope::intersects(const Envelope* other) const
{
    return other->minx <= maxx &&
           other->maxx >= minx &&
           other->miny <= maxy &&
           other->maxy >= miny;
}

/*public*/
INLINE bool
Envelope::intersects(double x, double y) const
{
    return (x <= maxx && x >= minx && y <= maxy && y >= miny);
}

/*public*/
INLINE bool
Envelope::disjoint(const Envelope& other) const
{
    return disjoint(&other);
}

/*public*/
INLINE bool
Envelope::disjoint(const Envelope* other) const
{
    return !(other->minx <= maxx ||
             other->maxx >= minx ||
             other->miny <= maxy ||
             other->maxy >= miny);
}


/*public*/
INLINE bool
Envelope::covers(const Coordinate* p) const
{
    return covers(p->x, p->y);
}

/*public*/
INLINE void
Envelope::setToNull()
{
    minx = maxx = miny = maxy = DoubleNotANumber;
}

INLINE double
Envelope::distanceSquared(const Envelope& env) const {
    double dx = std::max(0.0,
                         std::max(maxx, env.maxx) - std::min(minx, env.minx) - (maxx - minx) -
                         (env.maxx - env.minx));
    double dy = std::max(0.0,
                         std::max(maxy, env.maxy) - std::min(miny, env.miny) - (maxy - miny) -
                         (env.maxy - env.miny));

    return dx * dx + dy * dy;
}

INLINE double
Envelope::distance(const Envelope& env) const {
    return std::sqrt(distanceSquared(env));
}

INLINE double
Envelope::distanceToCoordinate(const Coordinate & c, const Coordinate & p0, const Coordinate & p1) {
    return std::sqrt(distanceSquaredToCoordinate(c, p0, p1));
}

INLINE double
Envelope::distanceSquaredToCoordinate(const Coordinate & c, const Coordinate & p0, const Coordinate & p1) {
    double xa = c.x - p0.x;
    double xb = c.x - p1.x;
    double ya = c.y - p0.y;
    double yb = c.y - p1.y;

    // If sign of a and b are not the same, then Envelope spans c and distance is zero.
    double dx = (std::signbit(xa) == std::signbit(xb)) * std::min(std::abs(xa), std::abs(xb));
    double dy = (std::signbit(ya) == std::signbit(yb)) * std::min(std::abs(ya), std::abs(yb));

    return dx*dx + dy*dy;
}

INLINE
bool operator!=(const Envelope& a, const Envelope& b) {
    return !(a == b);
}

INLINE bool
Envelope::intersects(const Coordinate& p1, const Coordinate& p2,
                     const Coordinate& q1, const Coordinate& q2)
{
    double minq = std::min(q1.x, q2.x);
    double maxq = std::max(q1.x, q2.x);
    double minp = std::min(p1.x, p2.x);
    double maxp = std::max(p1.x, p2.x);
    if(minp > maxq) {
        return false;
    }
    if(maxp < minq) {
        return false;
    }
    minq = std::min(q1.y, q2.y);
    maxq = std::max(q1.y, q2.y);
    minp = std::min(p1.y, p2.y);
    maxp = std::max(p1.y, p2.y);
    if(minp > maxq) {
        return false;
    }
    if(maxp < minq) {
        return false;
    }
    return true;
}


    } // namespace geos::geom
} // namespace geos

#endif // GEOS_GEOM_ENVELOPE_INL

