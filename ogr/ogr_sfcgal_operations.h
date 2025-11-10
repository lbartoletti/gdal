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
/*  Static class providing all SFCGAL operations with proper error     */
/*  handling, thread safety, and resource management                   */
/************************************************************************/

class CPL_DLL OGRSFCGALOperations
{
  private:
    // Thread-safe initialization
    static void EnsureInitialized();

  public:
    // Conversion helpers
    static OGRSFCGALGeometryPtr ToSFCGAL(const OGRGeometry *poGeom);
    static OGRGeometry *FromSFCGAL(const sfcgal_geometry_t *geom);

    // ===== Existing operations (refactored from OGRGeometry) =====

    /** Validate geometry using SFCGAL */
    static bool IsValid(const OGRGeometry *poGeom);

    /** 2D distance using SFCGAL */
    static double Distance(const OGRGeometry *poGeom1,
                           const OGRGeometry *poGeom2);

    /** 3D distance using SFCGAL */
    static double Distance3D(const OGRGeometry *poGeom1,
                             const OGRGeometry *poGeom2);

    /** 3D area calculation */
    static double Area3D(const OGRGeometry *poGeom);

    /** 3D convex hull */
    static OGRGeometry *ConvexHull3D(const OGRGeometry *poGeom);

    /** 3D intersection */
    static OGRGeometry *Intersection3D(const OGRGeometry *poGeom1,
                                       const OGRGeometry *poGeom2);

    /** 3D union */
    static OGRGeometry *Union3D(const OGRGeometry *poGeom1,
                                const OGRGeometry *poGeom2);

    /** 3D difference */
    static OGRGeometry *Difference3D(const OGRGeometry *poGeom1,
                                     const OGRGeometry *poGeom2);

    /** 3D intersection test */
    static bool Intersects3D(const OGRGeometry *poGeom1,
                             const OGRGeometry *poGeom2);

    // ===== NEW operations =====

    /** 3D buffer operation
     *
     * Creates a 3D buffer around the geometry at the specified distance.
     * Works on both 2D and 3D geometries.
     *
     * @param poGeom Input geometry (must not be NULL)
     * @param dfDistance Buffer distance (same units as geometry)
     * @return Buffered geometry as PolyhedralSurface, or nullptr on error
     *
     * @since GDAL 3.11
     */
    static OGRGeometry *Buffer3D(const OGRGeometry *poGeom, double dfDistance);

    /** Compute straight skeleton (2D only)
     *
     * The straight skeleton is the locus of points having more than one
     * closest point on the boundary. Used in architecture for roof design,
     * urban planning, and computational geometry.
     *
     * @param poGeom Input polygon (must be 2D, no Z coordinates)
     * @return MultiLineString representing the skeleton, or nullptr on error
     *
     * @note Only works on 2D polygons. Will return error if Is3D() == TRUE.
     * @note The input must be a simple polygon (no self-intersections).
     *
     * @since GDAL 3.11
     */
    static OGRGeometry *StraightSkeleton(const OGRGeometry *poGeom);

    /** Compute approximate medial axis (2D only)
     *
     * The medial axis (also known as topological skeleton) is the set of
     * points having more than one closest point on the boundary, represented
     * as a graph structure.
     *
     * @param poGeom Input polygon (must be 2D, no Z coordinates)
     * @return MultiLineString representing the approximate medial axis,
     *         or nullptr on error
     *
     * @note Only works on 2D polygons. Will return error if Is3D() == TRUE.
     * @note This is an approximation; exact medial axis computation is
     *       computationally expensive.
     *
     * @since GDAL 3.11
     */
    static OGRGeometry *ApproximateMedialAxis(const OGRGeometry *poGeom);

    // Utility: Check if SFCGAL is available
    static bool IsAvailable();
};

#endif  // HAVE_SFCGAL

#endif  // OGR_SFCGAL_OPERATIONS_H
