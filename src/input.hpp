#ifndef OSM2PGSQL_INPUT_HPP
#define OSM2PGSQL_INPUT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * It contains the functions reading and checking the input data.
 */

#include <string>
#include <vector>

#include <osmium/fwd.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/header.hpp>
#include <osmium/osm/types.hpp>

#include "osmtypes.hpp"

class osmdata_t;

struct type_id
{
    osmium::item_type type;
    osmid_t id;
    osmium::object_version_type version = 0;
};

struct file_info
{
    osmium::io::Header header;
    osmium::Timestamp last_timestamp;
};

/**
 * Compare two objects by (type, id). Throw a descriptive error if either the
 * curr id is negative or if the data is not ordered. With multiple_versions
 * the same object may follow itself if its version is higher.
 */
type_id check_input(type_id const &last, type_id curr,
                    bool multiple_versions = false);

type_id check_input(type_id const &last, osmium::OSMObject const &object,
                    bool multiple_versions = false);

/**
 * Prepare input file(s). Does format checks as far as this is possible
 * without actually opening the files. With multiple_versions (a create with
 * --history) files holding several versions of an object are accepted.
 */
std::vector<osmium::io::File>
prepare_input_files(std::vector<std::string> const &input_files,
                    std::string const &input_format, bool append,
                    bool multiple_versions);

/**
 * Process the specified OSM files (stage 1a).
 *
 * With multiple_versions (a create with --history) the input may hold several
 * versions of an object, sorted by version. Only the latest version of each
 * object is processed, and only if it is visible.
 */
file_info process_files(std::vector<osmium::io::File> const &files,
                        osmdata_t *osmdata, bool append,
                        bool multiple_versions, bool show_progress);

#endif // OSM2PGSQL_INPUT_HPP
