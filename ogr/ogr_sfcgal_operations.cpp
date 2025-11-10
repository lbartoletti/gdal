/******************************************************************************
 * Project:  OGR SFCGAL Integration
 * Purpose:  SFCGAL operations implementation
 * Author:   GDAL Development Team
 ******************************************************************************
 * Copyright (c) 2025, GDAL Development Team
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "ogr_sfcgal_operations.h"

#ifdef HAVE_SFCGAL

#include "ogr_sfcgal.h"
#include "cpl_error.h"
#include "ogr_api.h"
#include "ogr_p.h"
#include <mutex>
#include <cstdarg>
#include <memory>

/************************************************************************/
/*                        Static initialization                         */
/************************************************************************/

namespace
{
std::once_flag g_sfcgalInitFlag;

int SFCGALWarningHandler(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    CPLErrorV(CE_Warning, CPLE_AppDefined, msg, args);
    va_end(args);
    return 0;
}

int SFCGALErrorHandler(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    CPLErrorV(CE_Failure, CPLE_AppDefined, msg, args);
    va_end(args);
    return 0;
}
}  // anonymous namespace

/************************************************************************/
/*                        EnsureInitialized()                           */
/************************************************************************/

void OGRSFCGALOperations::EnsureInitialized()
{
    std::call_once(g_sfcgalInitFlag,
                   []()
                   {
                       sfcgal_init();
                       sfcgal_set_error_handlers(SFCGALWarningHandler,
                                                 SFCGALErrorHandler);
                   });
}

/************************************************************************/
/*                            ToSFCGAL()                                */
/************************************************************************/

OGRSFCGALGeometryPtr OGRSFCGALOperations::ToSFCGAL(const OGRGeometry *poGeom)
{
    if (!poGeom)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "NULL geometry passed to SFCGAL conversion");
        return OGRSFCGALGeometryPtr(nullptr);
    }

    EnsureInitialized();

    // Handle special geometry types that need conversion
    std::unique_ptr<OGRGeometry> poTempGeom;

    if (EQUAL(poGeom->getGeometryName(), "LINEARRING"))
    {
        poTempGeom.reset(
            OGRCurve::CastToLineString(poGeom->clone()->toCurve()));
        if (!poTempGeom)
            return OGRSFCGALGeometryPtr(nullptr);
        poGeom = poTempGeom.get();
    }
    else if (EQUAL(poGeom->getGeometryName(), "CIRCULARSTRING") ||
             EQUAL(poGeom->getGeometryName(), "COMPOUNDCURVE"))
    {
        poTempGeom.reset(
            OGRGeometryFactory::forceToLineString(poGeom->clone())
                ->toLineString());
        if (!poTempGeom)
            return OGRSFCGALGeometryPtr(nullptr);
        poGeom = poTempGeom.get();
    }
    else if (EQUAL(poGeom->getGeometryName(), "CURVEPOLYGON"))
    {
        poTempGeom.reset(
            OGRGeometryFactory::forceToPolygon(
                poGeom->clone()->toCurvePolygon())
                ->toPolygon());
        if (!poTempGeom)
            return OGRSFCGALGeometryPtr(nullptr);
        poGeom = poTempGeom.get();
    }

    // WKB-based conversion (SFCGAL >= 1.5.2)
    const size_t nSize = poGeom->WkbSize();
    unsigned char *pabyWkb =
        static_cast<unsigned char *>(CPLMalloc(nSize));
    if (!pabyWkb)
    {
        return OGRSFCGALGeometryPtr(nullptr);
    }

    OGRwkbExportOptions oOptions;
    oOptions.eByteOrder = wkbNDR;
    oOptions.eWkbVariant = wkbVariantIso;

    sfcgal_geometry_t *sfcgalGeom = nullptr;
    if (poGeom->exportToWkb(pabyWkb, &oOptions) == OGRERR_NONE)
    {
        sfcgalGeom = sfcgal_io_read_wkb(
            reinterpret_cast<const char *>(pabyWkb), nSize);
    }

    CPLFree(pabyWkb);
    return OGRSFCGALGeometryPtr(sfcgalGeom);
}

/************************************************************************/
/*                           FromSFCGAL()                               */
/************************************************************************/

OGRGeometry *OGRSFCGALOperations::FromSFCGAL(const sfcgal_geometry_t *geometry)
{
    if (!geometry)
    {
        return nullptr;
    }

    EnsureInitialized();

    char *pabySFCGAL = nullptr;
    size_t nLength = 0;

    sfcgal_geometry_as_wkb(geometry, &pabySFCGAL, &nLength);

    if (!pabySFCGAL || nLength == 0)
    {
        return nullptr;
    }

    OGRGeometry *poGeom = nullptr;
    const OGRErr eErr = OGRGeometryFactory::createFromWkb(
        reinterpret_cast<unsigned char *>(pabySFCGAL), nullptr, &poGeom,
        nLength);

    free(pabySFCGAL);

    return (eErr == OGRERR_NONE) ? poGeom : nullptr;
}

/************************************************************************/
/*                            IsValid()                                 */
/************************************************************************/

bool OGRSFCGALOperations::IsValid(const OGRGeometry *poGeom)
{
    auto poThis = ToSFCGAL(poGeom);
    if (!poThis)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Failed to convert geometry to SFCGAL format");
        return false;
    }

    const int res = sfcgal_geometry_is_valid(poThis.get());
    return res == 1;
}

/************************************************************************/
/*                            Distance()                                */
/************************************************************************/

double OGRSFCGALOperations::Distance(const OGRGeometry *poGeom1,
                                     const OGRGeometry *poGeom2)
{
    if (!poGeom1 || !poGeom2)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "NULL geometry passed to Distance");
        return -1.0;
    }

    auto poThis = ToSFCGAL(poGeom1);
    if (!poThis)
    {
        return -1.0;
    }

    auto poOther = ToSFCGAL(poGeom2);
    if (!poOther)
    {
        return -1.0;
    }

    const double dfDistance = sfcgal_geometry_distance(poThis.get(), poOther.get());
    return dfDistance > 0.0 ? dfDistance : -1.0;
}

/************************************************************************/
/*                           Distance3D()                               */
/************************************************************************/

double OGRSFCGALOperations::Distance3D(const OGRGeometry *poGeom1,
                                       const OGRGeometry *poGeom2)
{
    if (!poGeom1 || !poGeom2)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "NULL geometry passed to Distance3D");
        return -1.0;
    }

    if (!(poGeom1->Is3D() && poGeom2->Is3D()))
    {
        CPLDebug("OGR", "OGRGeometry::Distance3D called with two dimensional "
                        "geometry(geometries)");
        return -1.0;
    }

    auto poThis = ToSFCGAL(poGeom1);
    if (!poThis)
    {
        return -1.0;
    }

    auto poOther = ToSFCGAL(poGeom2);
    if (!poOther)
    {
        return -1.0;
    }

    const double dfDistance =
        sfcgal_geometry_distance_3d(poThis.get(), poOther.get());

    return dfDistance > 0 ? dfDistance : -1.0;
}

/************************************************************************/
/*                            Area3D()                                  */
/************************************************************************/

double OGRSFCGALOperations::Area3D(const OGRGeometry *poGeom)
{
    auto poThis = ToSFCGAL(poGeom);
    if (!poThis)
    {
        return -1.0;
    }

    const double area = sfcgal_geometry_area_3d(poThis.get());
    return (area > 0) ? area : -1.0;
}

/************************************************************************/
/*                          ConvexHull3D()                              */
/************************************************************************/

OGRGeometry *OGRSFCGALOperations::ConvexHull3D(const OGRGeometry *poGeom)
{
    auto poThis = ToSFCGAL(poGeom);
    if (!poThis)
    {
        return nullptr;
    }

    OGRSFCGALGeometryPtr poRes(sfcgal_geometry_convexhull_3d(poThis.get()));
    if (!poRes)
    {
        return nullptr;
    }

    OGRGeometry *poResult = FromSFCGAL(poRes.get());
    if (poResult && poGeom->getSpatialReference())
    {
        poResult->assignSpatialReference(poGeom->getSpatialReference());
    }

    return poResult;
}

/************************************************************************/
/*                         Intersection3D()                             */
/************************************************************************/

OGRGeometry *OGRSFCGALOperations::Intersection3D(const OGRGeometry *poGeom1,
                                                 const OGRGeometry *poGeom2)
{
    if (!poGeom1 || !poGeom2)
    {
        return nullptr;
    }

    auto poThis = ToSFCGAL(poGeom1);
    if (!poThis)
    {
        return nullptr;
    }

    auto poOther = ToSFCGAL(poGeom2);
    if (!poOther)
    {
        return nullptr;
    }

    OGRSFCGALGeometryPtr poRes(
        sfcgal_geometry_intersection_3d(poThis.get(), poOther.get()));
    if (!poRes)
    {
        return nullptr;
    }

    OGRGeometry *poResult = FromSFCGAL(poRes.get());
    if (poResult && poGeom1->getSpatialReference() &&
        poGeom2->getSpatialReference() &&
        poGeom2->getSpatialReference()->IsSame(poGeom1->getSpatialReference()))
    {
        poResult->assignSpatialReference(poGeom1->getSpatialReference());
    }

    return poResult;
}

/************************************************************************/
/*                            Union3D()                                 */
/************************************************************************/

OGRGeometry *OGRSFCGALOperations::Union3D(const OGRGeometry *poGeom1,
                                          const OGRGeometry *poGeom2)
{
    if (!poGeom1 || !poGeom2)
    {
        return nullptr;
    }

    auto poThis = ToSFCGAL(poGeom1);
    if (!poThis)
    {
        return nullptr;
    }

    auto poOther = ToSFCGAL(poGeom2);
    if (!poOther)
    {
        return nullptr;
    }

    OGRSFCGALGeometryPtr poRes(
        sfcgal_geometry_union_3d(poThis.get(), poOther.get()));
    if (!poRes)
    {
        return nullptr;
    }

    OGRGeometry *poResult = FromSFCGAL(poRes.get());
    if (poResult && poGeom1->getSpatialReference() &&
        poGeom2->getSpatialReference() &&
        poGeom2->getSpatialReference()->IsSame(poGeom1->getSpatialReference()))
    {
        poResult->assignSpatialReference(poGeom1->getSpatialReference());
    }

    return poResult;
}

/************************************************************************/
/*                          Difference3D()                              */
/************************************************************************/

OGRGeometry *OGRSFCGALOperations::Difference3D(const OGRGeometry *poGeom1,
                                               const OGRGeometry *poGeom2)
{
    if (!poGeom1 || !poGeom2)
    {
        return nullptr;
    }

    auto poThis = ToSFCGAL(poGeom1);
    if (!poThis)
    {
        return nullptr;
    }

    auto poOther = ToSFCGAL(poGeom2);
    if (!poOther)
    {
        return nullptr;
    }

    OGRSFCGALGeometryPtr poRes(
        sfcgal_geometry_difference_3d(poThis.get(), poOther.get()));
    if (!poRes)
    {
        return nullptr;
    }

    OGRGeometry *poResult = FromSFCGAL(poRes.get());
    if (poResult && poGeom1->getSpatialReference() &&
        poGeom2->getSpatialReference() &&
        poGeom2->getSpatialReference()->IsSame(poGeom1->getSpatialReference()))
    {
        poResult->assignSpatialReference(poGeom1->getSpatialReference());
    }

    return poResult;
}

/************************************************************************/
/*                          Intersects3D()                              */
/************************************************************************/

bool OGRSFCGALOperations::Intersects3D(const OGRGeometry *poGeom1,
                                       const OGRGeometry *poGeom2)
{
    if (!poGeom1 || !poGeom2)
    {
        return false;
    }

    auto poThis = ToSFCGAL(poGeom1);
    if (!poThis)
    {
        return false;
    }

    auto poOther = ToSFCGAL(poGeom2);
    if (!poOther)
    {
        return false;
    }

    const int res =
        sfcgal_geometry_intersects_3d(poThis.get(), poOther.get());
    return res == 1;
}

/************************************************************************/
/*                    ===== NEW OPERATIONS =====                        */
/************************************************************************/

/************************************************************************/
/*                            Buffer3D()                                */
/************************************************************************/

OGRGeometry *OGRSFCGALOperations::Buffer3D(const OGRGeometry *poGeom,
                                           double dfDistance)
{
    CPL_IGNORE_RET_VAL(poGeom);
    CPL_IGNORE_RET_VAL(dfDistance);

    // Note: SFCGAL does not provide a direct 3D buffer function in its C API
    // The sfcgal_geometry_buffer function does not exist or is not available
    // in the standard SFCGAL distribution.
    // A true 3D buffer would require computing the Minkowski sum with a sphere,
    // which is computationally expensive.
    // For now, we return an error indicating this is not yet implemented.

    CPLError(CE_Failure, CPLE_NotSupported,
             "Buffer3D is not yet implemented. "
             "SFCGAL does not provide a direct 3D buffer function in its C API. "
             "For 2D buffer, use Buffer() instead (GEOS).");
    return nullptr;
}

/************************************************************************/
/*                         StraightSkeleton()                           */
/************************************************************************/

OGRGeometry *OGRSFCGALOperations::StraightSkeleton(const OGRGeometry *poGeom)
{
    if (!poGeom)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "NULL geometry passed to StraightSkeleton");
        return nullptr;
    }

    // Validate 2D-only requirement
    if (poGeom->Is3D())
    {
        CPLError(
            CE_Failure, CPLE_AppDefined,
            "StraightSkeleton only works on 2D geometries. Use flattenTo2D() "
            "first.");
        return nullptr;
    }

    // Must be a polygon
    const OGRwkbGeometryType eType = wkbFlatten(poGeom->getGeometryType());
    if (eType != wkbPolygon)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "StraightSkeleton only works on Polygon geometries (got %s)",
                 poGeom->getGeometryName());
        return nullptr;
    }

    auto poThis = ToSFCGAL(poGeom);
    if (!poThis)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Failed to convert geometry to SFCGAL format");
        return nullptr;
    }

    // Compute straight skeleton
    OGRSFCGALGeometryPtr poRes(sfcgal_geometry_straight_skeleton(poThis.get()));

    if (!poRes)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "SFCGAL StraightSkeleton operation failed");
        return nullptr;
    }

    OGRGeometry *poResult = FromSFCGAL(poRes.get());
    if (poResult && poGeom->getSpatialReference())
    {
        poResult->assignSpatialReference(poGeom->getSpatialReference());
    }

    return poResult;
}

/************************************************************************/
/*                      ApproximateMedialAxis()                         */
/************************************************************************/

OGRGeometry *
OGRSFCGALOperations::ApproximateMedialAxis(const OGRGeometry *poGeom)
{
    if (!poGeom)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "NULL geometry passed to ApproximateMedialAxis");
        return nullptr;
    }

    // Validate 2D-only requirement
    if (poGeom->Is3D())
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "ApproximateMedialAxis only works on 2D geometries. Use "
                 "flattenTo2D() first.");
        return nullptr;
    }

    // Must be a polygon
    const OGRwkbGeometryType eType = wkbFlatten(poGeom->getGeometryType());
    if (eType != wkbPolygon)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined,
            "ApproximateMedialAxis only works on Polygon geometries (got %s)",
            poGeom->getGeometryName());
        return nullptr;
    }

    auto poThis = ToSFCGAL(poGeom);
    if (!poThis)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Failed to convert geometry to SFCGAL format");
        return nullptr;
    }

    // Compute approximate medial axis
    OGRSFCGALGeometryPtr poRes(
        sfcgal_geometry_approximate_medial_axis(poThis.get()));

    if (!poRes)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "SFCGAL ApproximateMedialAxis operation failed");
        return nullptr;
    }

    OGRGeometry *poResult = FromSFCGAL(poRes.get());
    if (poResult && poGeom->getSpatialReference())
    {
        poResult->assignSpatialReference(poGeom->getSpatialReference());
    }

    return poResult;
}

/************************************************************************/
/*                          IsAvailable()                               */
/************************************************************************/

bool OGRSFCGALOperations::IsAvailable()
{
    return true;
}

#endif  // HAVE_SFCGAL
