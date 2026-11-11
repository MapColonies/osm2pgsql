#ifndef OSM2PGSQL_EXPIRE_TILES_HPP
#define OSM2PGSQL_EXPIRE_TILES_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "expire-config.hpp"
#include "expire-output.hpp"
#include "geom.hpp"
#include "geom-box.hpp"
#include "logging.hpp"
#include "osmtypes.hpp"
#include "pgsql.hpp"
#include "tile.hpp"

class reprojection_t;

class expire_tiles_t
{
public:
    expire_tiles_t(uint32_t max_zoom,
                   std::shared_ptr<reprojection_t> projection,
                   std::size_t max_tiles_geometry = DEFAULT_MAX_TILES_GEOMETRY);

    bool enabled() const noexcept { return m_maxzoom != 0; }

    /**
     * Can geometries in this SRID be expired? Only Web Mercator (3857) and
     * WGS84 (4326) are supported: both are cylindrical, so a bounding box in
     * either maps to a bounding box in tile coordinates.
     */
    static bool supports_srid(int srid) noexcept;

    /**
     * Expire tiles for a geometry in the given SRID. The overloads without
     * an SRID take the geometry to be in the projection given to the
     * constructor.
     *
     * \pre supports_srid(srid)
     */
    template <typename TGEOM>
    void from_geometry(TGEOM const &geom, expire_config_t const &expire_config,
                       int srid)
    {
        projection_guard_t const guard{this, srid};
        from_geometry(geom, expire_config);
    }

    void from_polygon_boundary(geom::polygon_t const &geom,
                               expire_config_t const &expire_config);

    void from_polygon_area(geom::polygon_t const &geom);

    void from_geometry(geom::nullgeom_t const & /*geom*/,
                       expire_config_t const & /*expire_config*/)
    {}

    void from_geometry(geom::point_t const &geom,
                       expire_config_t const &expire_config);

    void from_geometry(geom::linestring_t const &geom,
                       expire_config_t const &expire_config);

    void from_geometry(geom::polygon_t const &geom,
                       expire_config_t const &expire_config);

    void from_geometry(geom::multipolygon_t const &geom,
                       expire_config_t const &expire_config);

    template <typename T>
    void from_geometry(geom::multigeometry_t<T> const &geom,
                       expire_config_t const &expire_config)
    {
        for (auto const &sgeom : geom) {
            from_geometry(sgeom, expire_config);
        }
    }

    void from_geometry(geom::geometry_t const &geom,
                       expire_config_t const &expire_config);

    void from_geometry_if_3857(geom::geometry_t const &geom,
                               expire_config_t const &expire_config);

    /**
     * Get tiles as a vector of quadkeys and remove them from the expire_tiles_t
     * object.
     */
    quadkey_list_t get_tiles();

    /**
     * Must be called after calling expire_tile() one or more times for a
     * single geometry to "commit" all tiles to be expired for that geometry.
     *
     * \param expire_output The expire output to write tiles to. If this is
     *                      the nullptr, nothing is done.
     */
    void commit_tiles(expire_output_t* expire_output);

private:
    /// Uses the projection for an SRID while it exists.
    class projection_guard_t
    {
    public:
        projection_guard_t(expire_tiles_t *expire_tiles, int srid);
        ~projection_guard_t() noexcept;

        projection_guard_t(projection_guard_t const &) = delete;
        projection_guard_t &operator=(projection_guard_t const &) = delete;
        projection_guard_t(projection_guard_t &&) = delete;
        projection_guard_t &operator=(projection_guard_t &&) = delete;

    private:
        expire_tiles_t *m_expire_tiles;
        reprojection_t const *m_previous;
    };

    /**
     * Converts from target coordinates to tile coordinates.
     */
    geom::point_t coords_to_tile(geom::point_t const &point);

    template <typename TGEOM>
    expire_mode decide_expire_mode(TGEOM const &geom,
                                   expire_config_t const &expire_config) const;

    /// Clamp a tile row or column to the map and convert it.
    uint32_t clamp_to_map(double tile) const noexcept;

    /**
     * Expire a single tile.
     *
     * \param x x index of the tile to be expired.
     * \param y y index of the tile to be expired.
     */
    void expire_tile(uint32_t x, uint32_t y);

    uint32_t normalise_tile_x_coord(int x) const;

    void build_tile_list(std::vector<uint32_t> *tile_x_list,
                         std::vector<geom::point_t> const &ring,
                         double tile_y) const;

    void from_line_segment(geom::point_t const &a, geom::point_t const &b,
                           expire_config_t const &expire_config);

    void from_point_list(geom::point_list_t const &list,
                         expire_config_t const &expire_config);

    /// This is where we collect all the expired tiles.
    std::unordered_set<quadkey_t> m_dirty_tiles;

    std::shared_ptr<reprojection_t> m_projection;

    /// Projection of the geometries passed in, see projection_guard_t.
    reprojection_t const *m_current_projection;

    std::size_t m_max_tiles_geometry;
    uint32_t m_maxzoom;
    int m_map_width;

}; // class expire_tiles_t

/**
 * Expire tiles based on an osm id.
 *
 * \param expire Where to mark expired tiles.
 * \param result Result of a database query into some table returning the
 *               geometries. (This is usually done using the "get_wkb"
 *               prepared statement.)
 * \param expire_config Configuration for expiry.
 * \return The number of tuples in the result or -1 if expire is disabled.
 */
int expire_from_result(expire_tiles_t *expire, pg_result_t const &result,
                       expire_config_t const &expire_config);

#endif // OSM2PGSQL_EXPIRE_TILES_HPP
