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
# Test Buffer3D - Note: Currently not implemented


def test_ogr_geom_sfcgal_buffer3d_not_implemented():
    """Test that Buffer3D returns not supported error"""

    if not ogrtest.have_sfcgal():
        pytest.skip("SFCGAL not available")

    # Buffer3D is not yet implemented in SFCGAL C API
    # It should raise a RuntimeError
    point = ogr.CreateGeometryFromWkt("POINT Z (0 0 0)")

    with gdal.quiet_errors():
        with pytest.raises(RuntimeError, match="Buffer3D is not yet implemented"):
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
