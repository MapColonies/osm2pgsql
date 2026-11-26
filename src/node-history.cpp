/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "node-history.hpp"

#include "format.hpp"
#include "logging.hpp"
#include "options.hpp"
#include "util.hpp"

#include <osmium/osm/node.hpp>
#include <osmium/osm/types_from_string.hpp>
#include <osmium/osm/way.hpp>

#include <cstdlib>
#include <unordered_map>

namespace {

std::string way_node_ids(osmium::Way const &way)
{
    util::string_joiner_t ids{',', '\0', '{', '}'};
    for (auto const &node_ref : way.nodes()) {
        ids.add(fmt::to_string(node_ref.ref()));
    }
    return ids();
}

} // anonymous namespace

node_history_t::node_history_t(options_t const &options)
: m_table(qualified_name(options.middle_dbschema,
                         options.prefix + "_node_versions")),
  m_middle_ways(options.slim ? qualified_name(options.middle_dbschema,
                                              options.prefix + "_ways")
                             : std::string{}),
  m_db_connection(options.connection_params, "node.history"),
  m_copy_thread(std::make_shared<db_copy_thread_t>(options.connection_params)),
  m_copy_mgr(m_copy_thread),
  m_target(std::make_shared<db_target_descr_t>(
      options.middle_dbschema, options.prefix + "_node_versions", "id",
      R"("id","version","ts","x","y")"))
{
    m_db_connection.exec("DROP TABLE IF EXISTS {}", m_table);
    m_db_connection.exec(
        "CREATE UNLOGGED TABLE {} (id int8 NOT NULL, version int4 NOT NULL,"
        " ts int8 NOT NULL, x int4 NOT NULL, y int4 NOT NULL,"
        " changed_with_way bool NOT NULL DEFAULT false)",
        m_table);
}

node_history_t::~node_history_t() noexcept
{
    if (m_finished) {
        return;
    }
    try {
        m_copy_mgr.sync();
        m_db_connection.exec("DROP TABLE IF EXISTS {}", m_table);
    } catch (...) { // NOLINT(bugprone-empty-catch)
        // A left-over table is dropped by the next create.
    }
}

void node_history_t::add(osmium::Node const &node)
{
    m_copy_mgr.new_line(m_target);
    m_copy_mgr.add_column(node.id());
    m_copy_mgr.add_column(node.version());
    m_copy_mgr.add_column(node.timestamp().seconds_since_epoch());
    m_copy_mgr.add_column(node.location().x());
    m_copy_mgr.add_column(node.location().y());
    m_copy_mgr.finish_line();
}

void node_history_t::after_nodes()
{
    m_copy_mgr.sync();

    log_info("Indexing node versions...");
    m_db_connection.exec("CREATE INDEX ON {} (id, ts)", m_table);
    m_db_connection.exec("ANALYZE {}", m_table);

    // A node version with the way's timestamp was changed together with the
    // way.
    auto const note = fmt::format("UPDATE {} SET changed_with_way = true"
                                  " WHERE id = ANY($1::int8[])"
                                  " AND ts = $2::int8 AND NOT changed_with_way",
                                  m_table);
    m_db_connection.prepare("note_changed_with_way", "{}", note);
    m_db_connection.prepare("locate_way_nodes",
                            "WITH c AS ({0})"
                            " SELECT DISTINCT ON (id) id, x, y FROM {1}"
                            " WHERE id = ANY($1::int8[]) AND ts <= $2::int8"
                            " ORDER BY id, ts DESC, version DESC",
                            note, m_table);
}

void node_history_t::locate(osmium::Way *way)
{
    auto const res = m_db_connection.exec_prepared(
        "locate_way_nodes", way_node_ids(*way),
        way->timestamp().seconds_since_epoch());

    std::unordered_map<osmid_t, osmium::Location> locations;
    for (int i = 0; i < res.num_tuples(); ++i) {
        locations.emplace(
            osmium::string_to_object_id(res.get_value(i, 0)),
            osmium::Location{
                static_cast<int32_t>(
                    std::strtol(res.get_value(i, 1), nullptr, 10)),
                static_cast<int32_t>(
                    std::strtol(res.get_value(i, 2), nullptr, 10))});
    }

    for (auto &node_ref : way->nodes()) {
        auto const it = locations.find(node_ref.ref());
        node_ref.set_location(it == locations.end() ? osmium::Location{}
                                                    : it->second);
    }
}

void node_history_t::note_changed_with(osmium::Way const &way)
{
    m_db_connection.exec_prepared("note_changed_with_way", way_node_ids(way),
                                  way.timestamp().seconds_since_epoch());
}

void node_history_t::finish()
{
    m_copy_mgr.sync();

    if (!m_middle_ways.empty()) {
        m_db_connection.exec(
            "UPDATE {} v SET changed_with_way = true"
            " FROM (SELECT unnest(nodes) AS node_id,"
            " EXTRACT(EPOCH FROM created)::int8 AS ts FROM {}) w"
            " WHERE v.id = w.node_id AND v.ts = w.ts"
            " AND NOT v.changed_with_way",
            m_table, m_middle_ways);
    }

    // Only nodes once changed together with a way are way nodes. A later
    // version of one that moved without a new version of a way is not in
    // history. Changes to tags only don't change any way.
    auto const res = m_db_connection.exec(
        "SELECT count(*) FROM (SELECT changed_with_way,"
        " bool_or(changed_with_way) OVER (PARTITION BY id) AS way_node,"
        " lag(x) OVER w AS prev_x, lag(y) OVER w AS prev_y, x, y FROM {}"
        " WINDOW w AS (PARTITION BY id ORDER BY ts, version)) v"
        " WHERE way_node AND NOT changed_with_way AND prev_x IS NOT NULL"
        " AND (x, y) <> (prev_x, prev_y)",
        m_table);
    auto const count = std::strtoull(res.get_value(0, 0), nullptr, 10);
    if (count > 0) {
        log_warn("{} node version(s) moved without a new version of a way"
                 " using the node. History does not record these changes.",
                 count);
    }

    m_db_connection.exec("DROP TABLE IF EXISTS {}", m_table);
    m_finished = true;
}
