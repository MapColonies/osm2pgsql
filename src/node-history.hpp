#ifndef OSM2PGSQL_NODE_HISTORY_HPP
#define OSM2PGSQL_NODE_HISTORY_HPP

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
 * It contains the node_history_t class.
 */

#include "db-copy-mgr.hpp"
#include "pgsql.hpp"

#include <osmium/fwd.hpp>

#include <memory>
#include <string>

struct options_t;

/**
 * Every version of every node, kept while a create reads a history file so
 * that earlier versions of ways get the node locations of their own time.
 *
 * The versions are copied into a table in the middle schema while the nodes
 * are read. The table is dropped when the create is done with it.
 */
class node_history_t
{
public:
    explicit node_history_t(options_t const &options);

    node_history_t(node_history_t const &) = delete;
    node_history_t &operator=(node_history_t const &) = delete;

    node_history_t(node_history_t &&) = delete;
    node_history_t &operator=(node_history_t &&) = delete;

    ~node_history_t() noexcept;

    /// Remember a visible version of a node.
    void add(osmium::Node const &node);

    /// Index the node versions once all nodes have been read.
    void after_nodes();

    /**
     * Set the locations of the way's nodes to those of the node versions
     * current at the way's timestamp, and note the node versions changed
     * together with the way.
     */
    void locate(osmium::Way *way);

    /**
     * Does the latest version of every way have to be passed to
     * note_changed_with()? Not with a database middle, which is read once
     * in finish() instead.
     */
    bool needs_latest_ways() const noexcept { return m_middle_ways.empty(); }

    /// Note the node versions changed together with the way.
    void note_changed_with(osmium::Way const &way);

    /**
     * Warn about node versions changed without a new version of a way using
     * the node, then drop the table.
     */
    void finish();

private:
    std::string m_table;

    /// The middle's ways table, or empty without a database middle.
    std::string m_middle_ways;

    pg_conn_t m_db_connection;
    std::shared_ptr<db_copy_thread_t> m_copy_thread;
    db_copy_mgr_t<db_deleter_by_id_t> m_copy_mgr;
    std::shared_ptr<db_target_descr_t> m_target;
    bool m_finished = false;
};

#endif // OSM2PGSQL_NODE_HISTORY_HPP
