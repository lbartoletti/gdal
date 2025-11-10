/******************************************************************************
 * Project:  OGR SFCGAL Integration
 * Purpose:  SFCGAL operations wrapper with RAII and thread safety
 * Author:   GDAL Development Team
 ******************************************************************************
 * Copyright (c) 2025, GDAL Development Team
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#ifndef OGR_SFCGAL_OPERATIONS_H
#define OGR_SFCGAL_OPERATIONS_H

#include "ogr_geometry.h"

#ifdef HAVE_SFCGAL

#include <SFCGAL/capi/sfcgal_c.h>
#include <mutex>

/************************************************************************/
/*                      OGRSFCGALGeometryPtr                            */
/*  RAII wrapper for sfcgal_geometry_t - automatic memory management   */
/************************************************************************/

class OGRSFCGALGeometryPtr
{
  private:
    sfcgal_geometry_t *m_geometry = nullptr;

  public:
    explicit OGRSFCGALGeometryPtr(sfcgal_geometry_t *geom = nullptr)
        : m_geometry(geom)
    {
    }

    ~OGRSFCGALGeometryPtr()
    {
        if (m_geometry)
        {
            sfcgal_geometry_delete(m_geometry);
        }
    }

    // Move semantics
    OGRSFCGALGeometryPtr(OGRSFCGALGeometryPtr &&other) noexcept
        : m_geometry(other.m_geometry)
    {
        other.m_geometry = nullptr;
    }

    OGRSFCGALGeometryPtr &operator=(OGRSFCGALGeometryPtr &&other) noexcept
    {
        if (this != &other)
        {
            if (m_geometry)
            {
                sfcgal_geometry_delete(m_geometry);
            }
            m_geometry = other.m_geometry;
            other.m_geometry = nullptr;
        }
        return *this;
    }

    // Delete copy operations
    OGRSFCGALGeometryPtr(const OGRSFCGALGeometryPtr &) = delete;
    OGRSFCGALGeometryPtr &operator=(const OGRSFCGALGeometryPtr &) = delete;

    // Access
    sfcgal_geometry_t *get() const
    {
        return m_geometry;
    }

    sfcgal_geometry_t *release()
    {
        auto tmp = m_geometry;
        m_geometry = nullptr;
        return tmp;
    }

    explicit operator bool() const
    {
        return m_geometry != nullptr;
    }
};

/************************************************************************/
/*                      OGRSFCGALOperations                             */
/************************************************************************/

class CPL_DLL OGRSFCGALOperations
{
  private:
    static void EnsureInitialized();

  public:
    static OGRSFCGALGeometryPtr ToSFCGAL(const OGRGeometry *poGeom);
    static OGRGeometry *FromSFCGAL(const sfcgal_geometry_t *geom);

    static bool IsValid(const OGRGeometry *poGeom);

    static double Distance(const OGRGeometry *poGeom1,
                           const OGRGeometry *poGeom2);

    static double Distance3D(const OGRGeometry *poGeom1,
                             const OGRGeometry *poGeom2);

    static double Area3D(const OGRGeometry *poGeom);

    static OGRGeometry *ConvexHull3D(const OGRGeometry *poGeom);

    static OGRGeometry *Intersection3D(const OGRGeometry *poGeom1,
                                       const OGRGeometry *poGeom2);

    static OGRGeometry *Union3D(const OGRGeometry *poGeom1,
                                const OGRGeometry *poGeom2);

    static OGRGeometry *Difference3D(const OGRGeometry *poGeom1,
                                     const OGRGeometry *poGeom2);

    static bool Intersects3D(const OGRGeometry *poGeom1,
                             const OGRGeometry *poGeom2);

    /** 3D buffer operation (requires SFCGAL 2.0.0+, not yet widely available)
     *
     * Computes a 3D buffer around a Point or LineString geometry.
     * The buffer radius is specified in the same units as the geometry.
     * The result would be a PolyhedralSurface.
     *
     * @param poGeom Input geometry (must be Point or LineString)
     * @param dfDistance Buffer distance (must be positive)
     * @return PolyhedralSurface representing the 3D buffer, or nullptr on error
     *
     * @note Currently returns CPLE_NotSupported error - requires SFCGAL 2.0.0 or later
     * @note This is a placeholder for future SFCGAL 2.0.0 support
     * @note For 2D buffer, use OGRGeometry::Buffer() instead (provided by GEOS)
     *
     * @since GDAL 3.13
     */
    static OGRGeometry *Buffer3D(const OGRGeometry *poGeom, double dfDistance);

    /** Compute straight skeleton
     *
     * The straight skeleton is a geometric structure derived from a polygon,
     * representing the locus of points equidistant from polygon edges.
     *
     * @param poGeom Input polygon (must be 2D)
     * @return MultiLineString representing the skeleton, or nullptr on error
     *
     * @note Only works on 2D Polygon geometries
     * @note The input must be a simple polygon (no self-intersections)
     *
     * @since GDAL 3.13
     */
    static OGRGeometry *StraightSkeleton(const OGRGeometry *poGeom);

    /** Compute approximate medial axis
     *
     * Returns the approximate medial axis of a polygon, based on the
     * straight skeleton with internal edges removed.
     *
     * @param poGeom Input polygon (must be 2D)
     * @return MultiLineString representing the medial axis, or nullptr on error
     *
     * @note Only works on 2D Polygon geometries
     * @note The result is the straight skeleton without the "arms" extending to vertices
     *
     * @since GDAL 3.13
     */
    static OGRGeometry *ApproximateMedialAxis(const OGRGeometry *poGeom);

    static bool IsAvailable();
};

#endif  // HAVE_SFCGAL

#endif  // OGR_SFCGAL_OPERATIONS_H
