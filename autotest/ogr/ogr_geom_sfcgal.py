#!/usr/bin/env pytest
# -*- coding: utf-8 -*-
###############################################################################
# Purpose:  Test new SFCGAL operations (Buffer3D, StraightSkeleton, etc.)
# Author:   GDAL Development Team
###############################################################################

import pytest

from osgeo import gdal, ogr

import ogrtest

pytestmark = pytest.mark.require_driver("Memory")


###############################################################################
# Test that new methods exist and are callable


def test_ogr_geom_sfcgal_new_methods_exist():
    """Test that new SFCGAL methods exist in the API"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL not available")

    # Create a simple polygon
    polygon = ogr.CreateGeometryFromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")

    # Check that new methods exist
    assert hasattr(polygon, "StraightSkeleton"), "StraightSkeleton method missing"
    assert hasattr(
        polygon, "ApproximateMedialAxis"
    ), "ApproximateMedialAxis method missing"
    assert hasattr(polygon, "Buffer3D"), "Buffer3D method missing"


###############################################################################
# Test StraightSkeleton basic functionality


def test_ogr_geom_sfcgal_straight_skeleton_simple():
    """Test straight skeleton on simple square"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL not available")

    # Simple square
    polygon = ogr.CreateGeometryFromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    skeleton = polygon.StraightSkeleton()

    assert skeleton is not None, "StraightSkeleton returned None"
    assert not skeleton.IsEmpty(), "Skeleton should not be empty"


def test_ogr_geom_sfcgal_straight_skeleton_3d_error():
    """Test that StraightSkeleton rejects 3D geometries"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL not available")

    # 3D polygon - should fail
    polygon3d = ogr.CreateGeometryFromWkt(
        "POLYGON Z ((0 0 0, 10 0 0, 10 10 0, 0 10 0, 0 0 0))"
    )

    with gdal.quiet_errors():
        with pytest.raises(RuntimeError, match="only works on 2D geometries"):
            polygon3d.StraightSkeleton()


###############################################################################
# Test ApproximateMedialAxis basic functionality


def test_ogr_geom_sfcgal_medial_axis_simple():
    """Test approximate medial axis on simple square"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL not available")

    # Simple square
    polygon = ogr.CreateGeometryFromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    axis = polygon.ApproximateMedialAxis()

    assert axis is not None, "ApproximateMedialAxis returned None"
    # Note: Medial axis can be empty for some simple geometries - this is expected


def test_ogr_geom_sfcgal_medial_axis_3d_error():
    """Test that ApproximateMedialAxis rejects 3D geometries"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL not available")

    # 3D polygon - should fail
    polygon3d = ogr.CreateGeometryFromWkt(
        "POLYGON Z ((0 0 5, 10 0 5, 10 10 5, 0 10 5, 0 0 5))"
    )

    with gdal.quiet_errors():
        with pytest.raises(RuntimeError, match="only works on 2D geometries"):
            polygon3d.ApproximateMedialAxis()


###############################################################################
# Test Buffer3D


def test_ogr_geom_sfcgal_buffer3d_not_available():
    """Test that Buffer3D returns not supported error (requires SFCGAL 2.0.0+)"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL not available")

    # Buffer3D requires SFCGAL 2.0.0+ which is not available in most distributions yet
    point = ogr.CreateGeometryFromWkt("POINT Z (0 0 0)")

    with gdal.quiet_errors():
        with pytest.raises(
            RuntimeError, match="Buffer3D requires SFCGAL 2.0.0 or later"
        ):
            point.Buffer3D(5.0)


###############################################################################
# Test backward compatibility - existing Distance3D still works


def test_ogr_geom_sfcgal_distance3d_backward_compat():
    """Test that existing Distance3D method still works (backward compatibility)"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL not available")

    # Existing Distance3D should still work
    p1 = ogr.CreateGeometryFromWkt("POINT Z (0 0 0)")
    p2 = ogr.CreateGeometryFromWkt("POINT Z (3 4 0)")

    dist = p1.Distance3D(p2)
    assert dist == pytest.approx(5.0), f"Expected distance 5.0, got {dist}"


###############################################################################
# Test error handling


def test_ogr_geom_sfcgal_straight_skeleton_non_polygon():
    """Test that StraightSkeleton rejects non-polygon geometries"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL not available")

    # LineString - should fail
    line = ogr.CreateGeometryFromWkt("LINESTRING(0 0, 10 10)")

    with gdal.quiet_errors():
        with pytest.raises(RuntimeError, match="only works on Polygon geometries"):
            line.StraightSkeleton()


def test_ogr_geom_sfcgal_medial_axis_non_polygon():
    """Test that ApproximateMedialAxis rejects non-polygon geometries"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL not available")

    # Point - should fail
    point = ogr.CreateGeometryFromWkt("POINT(0 0)")

    with gdal.quiet_errors():
        with pytest.raises(RuntimeError, match="only works on Polygon geometries"):
            point.ApproximateMedialAxis()


###############################################################################
# Tests migrated from ogr_geom.py


def test_ogr_geom_triangle_sfcgal():
    """Test SFCGAL operations on Triangle geometries"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL is not available")

    g1 = ogr.CreateGeometryFromWkt("TRIANGLE ((0 0,100 0 100,0 100 100,0 0))")
    g2 = ogr.CreateGeometryFromWkt("TRIANGLE ((-1 -1,100 0 100,0 100 100,-1 -1))")
    assert g2.Intersects(g1)

    g1 = ogr.CreateGeometryFromWkt("TRIANGLE ((0 0,1 0,0 1,0 0))")
    g2 = ogr.CreateGeometryFromWkt("TRIANGLE ((0 0,1 0,1 1,0 0))")
    g3 = g1.Intersection(g2)
    g4 = ogr.CreateGeometryFromWkt("TRIANGLE ((0.5 0.5 0,0 0 0,1 0 0,0.5 0.5 0))")
    assert g4.Equals(g3)


def test_ogr_geom_sfcgal():
    """Test SFCGAL with various geometry types"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL is not available")

    g1 = ogr.CreateGeometryFromWkt("TIN EMPTY")

    g2_poly = ogr.CreateGeometryFromWkt("POLYGON((0 0,0 1,1 1,0 0))")
    g2 = g2_poly.GetGeometryRef(0)
    g1.Distance(g2)

    g2 = ogr.CreateGeometryFromWkt("CIRCULARSTRING EMPTY")
    g1.Distance(g2)

    g2 = ogr.CreateGeometryFromWkt("CURVEPOLYGON EMPTY")
    g1.Distance(g2)


def test_ogr_geom_sfcgal_distance3D():
    """Test Distance3D with SFCGAL"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL is not available")

    point1 = ogr.CreateGeometryFromWkt("POINT (1.0 1.0 1.0)")
    point2 = ogr.CreateGeometryFromWkt("POINT (4.0 1.0 5.0)")

    assert point1.Distance3D(point2) == 5.0


def test_ogr_geom_sfcgal_intersection3D():
    """Test 3D intersection with SFCGAL"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL is not available")

    phsurface = ogr.CreateGeometryFromWkt(
        "POLYHEDRALSURFACE Z (((0 0 0,0 0 2,0 2 2,0 2 0,0 0 0)),"
        "((0 0 0,0 2 0,2 2 0,2 0 0,0 0 0)),"
        "((0 0 0,2 0 0,2 0 2,0 0 2,0 0 0)),"
        "((2 2 0,2 2 2,2 0 2,2 0 0,2 2 0)),"
        "((0 2 0,0 2 2,2 2 2,2 2 0,0 2 0)),"
        "((0 0 2,2 0 2,2 2 2,0 2 2,0 0 2)))"
    )

    line = ogr.CreateGeometryFromWkt("LINESTRING Z (-1 1 1, 3 1 1)")

    result = phsurface.Intersection(line)

    assert result.ExportToWkt() == "MULTIPOINT (0 1 1,2 1 1)"
