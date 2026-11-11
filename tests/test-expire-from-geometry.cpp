/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include <osmium/geom/mercator_projection.hpp>

#include <algorithm>
#include <memory>
#include <set>
#include <utility>

#include "expire-tiles.hpp"
#include "projection.hpp"
#include "reprojection.hpp"
#include "tile-output.hpp"
#include "tile.hpp"

namespace {

std::shared_ptr<reprojection_t> defproj{
    reprojection_t::create_projection(PROJ_SPHERE_MERC)};

std::shared_ptr<reprojection_t> latlonproj{
    reprojection_t::create_projection(PROJ_LATLONG)};

// We are using zoom level 12 here, because at that level a tile is about
// 10,000 units wide/high which gives us easy numbers to work with.
constexpr uint32_t ZOOM = 12;

geom::point_t merc_to_latlon(geom::point_t const &point)
{
    auto const c = osmium::geom::mercator_to_lonlat(
        osmium::geom::Coordinates{point.x(), point.y()});
    return {c.x, c.y};
}

template <typename TLIST>
TLIST merc_to_latlon(TLIST const &list)
{
    TLIST result;
    for (auto const &point : list) {
        result.push_back(merc_to_latlon(point));
    }
    return result;
}

template <typename TGEOM>
quadkey_list_t expire(std::shared_ptr<reprojection_t> const &projection,
                      TGEOM const &geom, expire_config_t const &expire_config)
{
    expire_tiles_t et{ZOOM, projection};
    et.from_geometry(geom, expire_config);
    return et.get_tiles();
}

} // anonymous namespace

TEST_CASE("expire null geometry does nothing", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles_t et{ZOOM, defproj};

    SECTION("geom")
    {
        geom::geometry_t const geom{};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::geometry_t geom{};
        geom.set_srid(PROJ_SPHERE_MERC);
        et.from_geometry_if_3857(geom, expire_config);
    }

    REQUIRE(et.get_tiles().empty());
}

TEST_CASE("expire point at tile boundary", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles_t et{ZOOM, defproj};

    geom::point_t const pt{0.0, 0.0};

    SECTION("point") { et.from_geometry(pt, expire_config); }

    SECTION("geom")
    {
        geom::geometry_t const geom{pt};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::geometry_t geom{pt};
        geom.set_srid(PROJ_SPHERE_MERC);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 4);
    CHECK(tile_t::from_quadkey(tiles[0], ZOOM) == tile_t{ZOOM, 2047, 2047});
    CHECK(tile_t::from_quadkey(tiles[1], ZOOM) == tile_t{ZOOM, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[2], ZOOM) == tile_t{ZOOM, 2047, 2048});
    CHECK(tile_t::from_quadkey(tiles[3], ZOOM) == tile_t{ZOOM, 2048, 2048});
}

TEST_CASE("expire point away from tile boundary", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles_t et{ZOOM, defproj};

    geom::point_t const pt{5000.0, 5000.0};

    SECTION("point") { et.from_geometry(pt, expire_config); }

    SECTION("geom")
    {
        geom::geometry_t const geom{pt};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::geometry_t geom{pt};
        geom.set_srid(PROJ_SPHERE_MERC);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 1);
    CHECK(tile_t::from_quadkey(tiles[0], ZOOM) == tile_t{ZOOM, 2048, 2047});
}

TEST_CASE("expire linestring away from tile boundary", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles_t et{ZOOM, defproj};

    SECTION("line")
    {
        geom::linestring_t const line{{5000.0, 4000.0}, {5100.0, 4200.0}};
        et.from_geometry(line, expire_config);
    }

    SECTION("geom")
    {
        geom::linestring_t line{{5000.0, 4000.0}, {5100.0, 4200.0}};
        geom::geometry_t const geom{std::move(line)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::linestring_t line{{5000.0, 4000.0}, {5100.0, 4200.0}};
        geom::geometry_t geom{std::move(line)};
        geom.set_srid(PROJ_SPHERE_MERC);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 1);
    CHECK(tile_t::from_quadkey(tiles[0], ZOOM) == tile_t{ZOOM, 2048, 2047});
}

TEST_CASE("expire linestring crossing tile boundary", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles_t et{ZOOM, defproj};

    SECTION("line")
    {
        geom::linestring_t const line{{5000.0, 5000.0}, {5000.0, 15000.0}};
        et.from_geometry(line, expire_config);
    }

    SECTION("geom")
    {
        geom::linestring_t line{{5000.0, 5000.0}, {5000.0, 15000.0}};
        geom::geometry_t const geom{std::move(line)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::linestring_t line{{5000.0, 5000.0}, {5000.0, 15000.0}};
        geom::geometry_t geom{std::move(line)};
        geom.set_srid(PROJ_SPHERE_MERC);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 2);
    CHECK(tile_t::from_quadkey(tiles[0], ZOOM) == tile_t{ZOOM, 2048, 2046});
    CHECK(tile_t::from_quadkey(tiles[1], ZOOM) == tile_t{ZOOM, 2048, 2047});
}

TEST_CASE("expire small polygon", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles_t et{ZOOM, defproj};

    SECTION("polygon")
    {
        geom::polygon_t const poly{{{5000.0, 5000.0},
                                    {5100.0, 5000.0},
                                    {5100.0, 5100.0},
                                    {5000.0, 5100.0},
                                    {5000.0, 5000.0}}};
        et.from_geometry(poly, expire_config);
    }

    SECTION("geom")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {5100.0, 5000.0},
                              {5100.0, 5100.0},
                              {5000.0, 5100.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t const geom{std::move(poly)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {5100.0, 5000.0},
                              {5100.0, 5100.0},
                              {5000.0, 5100.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t geom{std::move(poly)};
        geom.set_srid(PROJ_SPHERE_MERC);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 1);
    CHECK(tile_t::from_quadkey(tiles[0], ZOOM) == tile_t{ZOOM, 2048, 2047});
}

TEST_CASE("expire large polygon as bbox", "[NoDB]")
{
    expire_config_t expire_config;
    expire_config.mode = expire_mode::hybrid;
    expire_config.full_area_limit = 40000;
    expire_tiles_t et{ZOOM, defproj};

    SECTION("polygon")
    {
        geom::polygon_t const poly{{{5000.0, 5000.0},
                                    {25000.0, 5000.0},
                                    {25000.0, 25000.0},
                                    {5000.0, 25000.0},
                                    {5000.0, 5000.0}}};
        et.from_geometry(poly, expire_config);
    }

    SECTION("geom")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {25000.0, 5000.0},
                              {25000.0, 25000.0},
                              {5000.0, 25000.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t const geom{std::move(poly)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {25000.0, 5000.0},
                              {25000.0, 25000.0},
                              {5000.0, 25000.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t geom{std::move(poly)};
        geom.set_srid(PROJ_SPHERE_MERC);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 9);
    CHECK(tile_t::from_quadkey(tiles[0], ZOOM) == tile_t{ZOOM, 2048, 2045});
    CHECK(tile_t::from_quadkey(tiles[1], ZOOM) == tile_t{ZOOM, 2049, 2045});
    CHECK(tile_t::from_quadkey(tiles[2], ZOOM) == tile_t{ZOOM, 2050, 2045});

    CHECK(tile_t::from_quadkey(tiles[3], ZOOM) == tile_t{ZOOM, 2048, 2046});
    CHECK(tile_t::from_quadkey(tiles[4], ZOOM) == tile_t{ZOOM, 2049, 2046});
    CHECK(tile_t::from_quadkey(tiles[7], ZOOM) == tile_t{ZOOM, 2050, 2046});

    CHECK(tile_t::from_quadkey(tiles[5], ZOOM) == tile_t{ZOOM, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[6], ZOOM) == tile_t{ZOOM, 2049, 2047});
    CHECK(tile_t::from_quadkey(tiles[8], ZOOM) == tile_t{ZOOM, 2050, 2047});
}

TEST_CASE("expire large polygon as boundary", "[NoDB]")
{
    expire_config_t expire_config;
    expire_config.mode = expire_mode::hybrid;
    expire_config.full_area_limit = 10000;
    expire_tiles_t et{ZOOM, defproj};

    SECTION("polygon")
    {
        geom::polygon_t const poly{{{5000.0, 5000.0},
                                    {25000.0, 5000.0},
                                    {25000.0, 25000.0},
                                    {5000.0, 25000.0},
                                    {5000.0, 5000.0}}};
        et.from_geometry(poly, expire_config);
    }

    SECTION("polygon boundary")
    {
        geom::polygon_t const poly{{{5000.0, 5000.0},
                                    {25000.0, 5000.0},
                                    {25000.0, 25000.0},
                                    {5000.0, 25000.0},
                                    {5000.0, 5000.0}}};
        et.from_polygon_boundary(poly, expire_config);
    }

    SECTION("geom")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {25000.0, 5000.0},
                              {25000.0, 25000.0},
                              {5000.0, 25000.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t const geom{std::move(poly)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::polygon_t poly{{{5000.0, 5000.0},
                              {25000.0, 5000.0},
                              {25000.0, 25000.0},
                              {5000.0, 25000.0},
                              {5000.0, 5000.0}}};
        geom::geometry_t geom{std::move(poly)};
        geom.set_srid(PROJ_SPHERE_MERC);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 8);
    CHECK(tile_t::from_quadkey(tiles[0], ZOOM) == tile_t{ZOOM, 2048, 2045});
    CHECK(tile_t::from_quadkey(tiles[1], ZOOM) == tile_t{ZOOM, 2049, 2045});
    CHECK(tile_t::from_quadkey(tiles[2], ZOOM) == tile_t{ZOOM, 2050, 2045});

    CHECK(tile_t::from_quadkey(tiles[3], ZOOM) == tile_t{ZOOM, 2048, 2046});
    CHECK(tile_t::from_quadkey(tiles[6], ZOOM) == tile_t{ZOOM, 2050, 2046});

    CHECK(tile_t::from_quadkey(tiles[4], ZOOM) == tile_t{ZOOM, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[5], ZOOM) == tile_t{ZOOM, 2049, 2047});
    CHECK(tile_t::from_quadkey(tiles[7], ZOOM) == tile_t{ZOOM, 2050, 2047});
}

TEST_CASE("expire multipoint geometry", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles_t et{ZOOM, defproj};

    geom::point_t const p1{0.0, 0.0};
    geom::point_t const p2{15000.0, 15000.0};

    SECTION("multipoint")
    {
        geom::multipoint_t mpt;
        mpt.add_geometry(p1);
        mpt.add_geometry(p2);
        et.from_geometry(mpt, expire_config);
    }

    SECTION("geom")
    {
        geom::multipoint_t mpt;
        mpt.add_geometry(p1);
        mpt.add_geometry(p2);
        geom::geometry_t const geom{std::move(mpt)};
        et.from_geometry(geom, expire_config);
    }

    SECTION("geom with check")
    {
        geom::multipoint_t mpt;
        mpt.add_geometry(p1);
        mpt.add_geometry(p2);
        geom::geometry_t geom{std::move(mpt)};
        geom.set_srid(PROJ_SPHERE_MERC);
        et.from_geometry_if_3857(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 5);
    CHECK(tile_t::from_quadkey(tiles[0], ZOOM) == tile_t{ZOOM, 2047, 2047});
    CHECK(tile_t::from_quadkey(tiles[1], ZOOM) == tile_t{ZOOM, 2049, 2046});
    CHECK(tile_t::from_quadkey(tiles[2], ZOOM) == tile_t{ZOOM, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[3], ZOOM) == tile_t{ZOOM, 2047, 2048});
    CHECK(tile_t::from_quadkey(tiles[4], ZOOM) == tile_t{ZOOM, 2048, 2048});
}

TEST_CASE("expire multilinestring geometry", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles_t et{ZOOM, defproj};

    geom::linestring_t l1{{2000.0, 2000.0}, {3000.0, 3000.0}};
    geom::linestring_t l2{{15000.0, 15000.0}, {25000.0, 15000.0}};
    geom::multilinestring_t ml;
    ml.add_geometry(std::move(l1));
    ml.add_geometry(std::move(l2));

    SECTION("multilinestring") { et.from_geometry(ml, expire_config); }

    SECTION("geom")
    {
        geom::geometry_t const geom{std::move(ml)};
        et.from_geometry(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 3);
    CHECK(tile_t::from_quadkey(tiles[0], ZOOM) == tile_t{ZOOM, 2049, 2046});
    CHECK(tile_t::from_quadkey(tiles[1], ZOOM) == tile_t{ZOOM, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[2], ZOOM) == tile_t{ZOOM, 2050, 2046});
}

TEST_CASE("expire multipolygon geometry", "[NoDB]")
{
    expire_config_t expire_config;
    expire_config.mode = expire_mode::hybrid;
    expire_config.full_area_limit = 10000;
    expire_tiles_t et{ZOOM, defproj};

    geom::polygon_t p1{{{2000.0, 2000.0},
                        {2000.0, 3000.0},
                        {3000.0, 3000.0},
                        {3000.0, 2000.0},
                        {2000.0, 2000.0}}};

    geom::polygon_t p2{{{15000.0, 15000.0},
                        {45000.0, 15000.0},
                        {45000.0, 45000.0},
                        {15000.0, 45000.0},
                        {15000.0, 15000.0}}};
    p2.add_inner_ring({{25000.0, 25000.0},
                       {25000.0, 35000.0},
                       {35000.0, 35000.0},
                       {35000.0, 25000.0},
                       {25000.0, 25000.0}});

    geom::multipolygon_t mp;
    mp.add_geometry(std::move(p1));
    mp.add_geometry(std::move(p2));

    SECTION("multilinestring") { et.from_geometry(mp, expire_config); }

    SECTION("geom")
    {
        geom::geometry_t const geom{std::move(mp)};
        et.from_geometry(geom, expire_config);
    }

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 17);

    std::set<quadkey_t> result;
    for (auto const &tile : tiles) {
        result.insert(tile);
    }

    std::set<quadkey_t> expected;
    expected.insert(tile_t{ZOOM, 2048, 2047}.quadkey()); // p1

    for (uint32_t x = 2049; x <= 2052; ++x) {
        for (uint32_t y = 2043; y <= 2046; ++y) {
            expected.insert(tile_t{ZOOM, x, y}.quadkey()); // p2
        }
    }
    REQUIRE(expected == result);
}

TEST_CASE("expire geometry collection", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles_t et{ZOOM, defproj};

    geom::collection_t collection;
    collection.add_geometry(geom::geometry_t{geom::point_t{0.0, 0.0}});
    collection.add_geometry(geom::geometry_t{
        geom::linestring_t{{15000.0, 15000.0}, {25000.0, 15000.0}}});

    geom::geometry_t const geom{std::move(collection)};
    et.from_geometry(geom, expire_config);

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 6);
    CHECK(tile_t::from_quadkey(tiles[0], ZOOM) == tile_t{ZOOM, 2047, 2047});
    CHECK(tile_t::from_quadkey(tiles[1], ZOOM) == tile_t{ZOOM, 2049, 2046});
    CHECK(tile_t::from_quadkey(tiles[2], ZOOM) == tile_t{ZOOM, 2048, 2047});
    CHECK(tile_t::from_quadkey(tiles[3], ZOOM) == tile_t{ZOOM, 2050, 2046});
    CHECK(tile_t::from_quadkey(tiles[4], ZOOM) == tile_t{ZOOM, 2047, 2048});
    CHECK(tile_t::from_quadkey(tiles[5], ZOOM) == tile_t{ZOOM, 2048, 2048});
}

TEST_CASE("expire works if in 3857", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles_t et{ZOOM, defproj};

    geom::geometry_t geom{geom::point_t{0.0, 0.0}};
    geom.set_srid(PROJ_SPHERE_MERC);
    et.from_geometry_if_3857(geom, expire_config);

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.size() == 4);
}

TEST_CASE("expire doesn't do anything if not in 3857", "[NoDB]")
{
    expire_config_t const expire_config;
    expire_tiles_t et{ZOOM, defproj};

    geom::geometry_t geom{geom::point_t{0.0, 0.0}};
    geom.set_srid(1234);
    et.from_geometry_if_3857(geom, expire_config);

    auto const tiles = et.get_tiles();
    REQUIRE(tiles.empty());
}

TEST_CASE("expire point in 4326 matches web mercator", "[NoDB]")
{
    expire_config_t const expire_config;
    geom::point_t const pt{5000.0, 5000.0};

    auto const merc_tiles = expire(defproj, pt, expire_config);
    REQUIRE(merc_tiles.size() == 1);
    REQUIRE(expire(latlonproj, merc_to_latlon(pt), expire_config) ==
            merc_tiles);
}

TEST_CASE("expire linestring in 4326 matches web mercator", "[NoDB]")
{
    expire_config_t const expire_config;
    geom::linestring_t const line{{5000.0, 5000.0}, {5000.0, 15000.0}};

    auto const merc_tiles = expire(defproj, line, expire_config);
    REQUIRE(merc_tiles.size() == 2);
    REQUIRE(expire(latlonproj, merc_to_latlon(line), expire_config) ==
            merc_tiles);
}

TEST_CASE("expire polygon boundary in 4326 matches web mercator", "[NoDB]")
{
    expire_config_t expire_config;
    expire_config.mode = expire_mode::boundary_only;
    geom::ring_t const ring{{5000.0, 5000.0},
                            {25000.0, 5000.0},
                            {25000.0, 25000.0},
                            {5000.0, 25000.0},
                            {5000.0, 5000.0}};

    auto const merc_tiles =
        expire(defproj, geom::polygon_t{geom::ring_t{ring}}, expire_config);
    REQUIRE(merc_tiles.size() == 8);
    REQUIRE(expire(latlonproj, geom::polygon_t{merc_to_latlon(ring)},
                   expire_config) == merc_tiles);
}

TEST_CASE("expire polygon interior in 4326 matches web mercator", "[NoDB]")
{
    expire_config_t expire_config;
    expire_config.mode = expire_mode::full_area;
    geom::ring_t const ring{{5000.0, 5000.0},
                            {25000.0, 5000.0},
                            {25000.0, 25000.0},
                            {5000.0, 25000.0},
                            {5000.0, 5000.0}};

    auto const merc_tiles =
        expire(defproj, geom::polygon_t{geom::ring_t{ring}}, expire_config);
    auto const latlon_tiles = expire(
        latlonproj, geom::polygon_t{merc_to_latlon(ring)}, expire_config);

    REQUIRE(merc_tiles.size() == 9);
    REQUIRE(latlon_tiles == merc_tiles);

    // The one tile no part of the boundary touches.
    auto const interior = tile_t{ZOOM, 2049, 2046}.quadkey();
    REQUIRE(std::find(latlon_tiles.cbegin(), latlon_tiles.cend(), interior) !=
            latlon_tiles.cend());
}

TEST_CASE("expire large 4326 polygon as boundary", "[NoDB]")
{
    // The limit is in Web Mercator units also for geometries in 4326, so the
    // same polygon is expired as boundary only in both projections.
    expire_config_t expire_config;
    expire_config.mode = expire_mode::hybrid;
    expire_config.full_area_limit = 10000;
    geom::ring_t const ring{{5000.0, 5000.0},
                            {25000.0, 5000.0},
                            {25000.0, 25000.0},
                            {5000.0, 25000.0},
                            {5000.0, 5000.0}};

    auto const merc_tiles =
        expire(defproj, geom::polygon_t{geom::ring_t{ring}}, expire_config);
    REQUIRE(merc_tiles.size() == 8);
    REQUIRE(expire(latlonproj, geom::polygon_t{merc_to_latlon(ring)},
                   expire_config) == merc_tiles);
}

TEST_CASE("expire geometries in 3857 and 4326 with one expire_tiles_t",
          "[NoDB]")
{
    expire_config_t const expire_config;
    geom::polygon_t const merc{{{5000.0, 5000.0},
                                {25000.0, 5000.0},
                                {25000.0, 25000.0},
                                {5000.0, 25000.0},
                                {5000.0, 5000.0}}};
    geom::polygon_t const latlon{merc_to_latlon(merc.outer())};

    auto const expected = expire(defproj, merc, expire_config);
    REQUIRE(expected.size() == 9);

    expire_tiles_t et{ZOOM, defproj};

    SECTION("3857 first")
    {
        et.from_geometry(merc, expire_config, PROJ_SPHERE_MERC);
        REQUIRE(et.get_tiles() == expected);
        et.from_geometry(latlon, expire_config, PROJ_LATLONG);
        REQUIRE(et.get_tiles() == expected);
    }

    SECTION("4326 first")
    {
        et.from_geometry(latlon, expire_config, PROJ_LATLONG);
        REQUIRE(et.get_tiles() == expected);
        et.from_geometry(merc, expire_config, PROJ_SPHERE_MERC);
        REQUIRE(et.get_tiles() == expected);
    }

    SECTION("the SRID only applies to its own call")
    {
        et.from_geometry(latlon, expire_config, PROJ_LATLONG);
        REQUIRE(et.get_tiles() == expected);
        et.from_geometry(merc, expire_config);
        REQUIRE(et.get_tiles() == expected);
    }
}

TEST_CASE("only 3857 and 4326 can be expired", "[NoDB]")
{
    REQUIRE(expire_tiles_t::supports_srid(PROJ_SPHERE_MERC));
    REQUIRE(expire_tiles_t::supports_srid(PROJ_LATLONG));
    REQUIRE_FALSE(expire_tiles_t::supports_srid(2039));
    REQUIRE_FALSE(expire_tiles_t::supports_srid(0));
}

TEST_CASE("expire point next to the antimeridian wraps around", "[NoDB]")
{
    // Lon -179.99 is in the leftmost column. A buffer of 1.5 tiles reaches
    // column -1, which is the rightmost column, 4095.
    expire_config_t expire_config;
    expire_config.buffer = 1.5;
    expire_tiles_t et{ZOOM, latlonproj};
    et.from_geometry(geom::point_t{-179.99, 0.01}, expire_config);

    std::set<uint32_t> columns;
    for (auto const quadkey : et.get_tiles()) {
        columns.insert(tile_t::from_quadkey(quadkey, ZOOM).x());
    }
    REQUIRE(columns == std::set<uint32_t>{0, 1, 4095});
}

TEST_CASE("expire 4326 polygon beyond the Web Mercator latitude limit",
          "[NoDB]")
{
    expire_config_t expire_config;
    expire_config.mode = expire_mode::full_area;
    expire_tiles_t et{4, latlonproj};

    geom::polygon_t const poly{{{0.0, -89.99},
                                {40.0, -89.99},
                                {40.0, -60.0},
                                {0.0, -60.0},
                                {0.0, -89.99}}};
    et.from_geometry(poly, expire_config);

    auto const tiles = et.get_tiles();
    REQUIRE_FALSE(tiles.empty());
    for (auto const quadkey : tiles) {
        auto const tile = tile_t::from_quadkey(quadkey, 4);
        CHECK(tile.x() < 16);
        CHECK(tile.y() < 16);
    }
}
