/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-table.hpp"
#include "format.hpp"
#include "logging.hpp"
#include "pgsql-capabilities.hpp"
#include "pgsql-helper.hpp"
#include "util.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <string>

char const *type_to_char(osmium::item_type type) noexcept
{
    switch (type) {
    case osmium::item_type::node:
        return "N";
    case osmium::item_type::way:
        return "W";
    case osmium::item_type::relation:
        return "R";
    default:
        break;
    }
    return "X";
}

bool flex_table_t::has_multicolumn_id_index() const noexcept
{
    return m_columns[0].type() == table_column_type::id_type;
}

std::string flex_table_t::id_column_names() const
{
    std::string name;

    if (!has_id_column()) {
        return name;
    }

    name = m_columns[0].name();
    if (has_multicolumn_id_index()) {
        name += ',';
        name += m_columns[1].name();
    }

    return name;
}

std::string flex_table_t::full_name() const
{
    return qualified_name(schema(), name());
}

std::string flex_table_t::full_history_name() const
{
    return qualified_name(schema(), history_name());
}

std::string flex_table_t::full_tmp_name() const
{
    return qualified_name(schema(), name() + "_tmp");
}

bool flex_table_t::has_id_column() const noexcept
{
    if (m_columns.empty()) {
        return false;
    }
    return (m_columns[0].type() == table_column_type::id_type) ||
           (m_columns[0].type() == table_column_type::id_num);
}

bool flex_table_t::matches_type(osmium::item_type type) const noexcept
{
    // This table takes any type or has no ids -> okay
    if (m_id_type == flex_table_index_type::any_object ||
        m_id_type == flex_table_index_type::no_index) {
        return true;
    }

    // Type and table type match -> okay
    if ((type == osmium::item_type::node &&
         m_id_type == flex_table_index_type::node) ||
        (type == osmium::item_type::way &&
         m_id_type == flex_table_index_type::way) ||
        (type == osmium::item_type::relation &&
         m_id_type == flex_table_index_type::relation)) {
        return true;
    }

    // Relations can be written as linestrings into way tables -> okay
    if (type == osmium::item_type::relation &&
        m_id_type == flex_table_index_type::way) {
        return true;
    }

    // Area tables can take ways or relations, but not nodes
    return m_id_type == flex_table_index_type::area &&
           type != osmium::item_type::node;
}

/// Map way/node/relation ID to id value used in database table column
osmid_t flex_table_t::map_id(osmium::item_type type, osmid_t id) const noexcept
{
    if (m_id_type == flex_table_index_type::any_object) {
        if (has_multicolumn_id_index()) {
            return id;
        }

        switch (type) {
        case osmium::item_type::node:
            return id;
        case osmium::item_type::way:
            return -id;
        case osmium::item_type::relation:
            return -id - 100000000000000000LL;
        default:
            assert(false);
        }
    }

    if (m_id_type != flex_table_index_type::relation &&
        type == osmium::item_type::relation) {
        return -id;
    }
    return id;
}

flex_table_column_t &flex_table_t::add_column(std::string const &name,
                                              std::string const &type,
                                              std::string const &sql_type)
{
    // id_type (optional) and id_num must always be the first columns
    assert(type != "id_type" || m_columns.empty());
    assert(type != "id_num" || m_columns.empty() ||
           (m_columns.size() == 1 &&
            m_columns[0].type() == table_column_type::id_type));

    auto &column = m_columns.emplace_back(name, type, sql_type);

    if (column.is_geometry_column()) {
        if (m_geom_column == std::numeric_limits<std::size_t>::max()) {
            m_geom_column = m_columns.size() - 1;
        } else {
            m_has_multiple_geom_columns = true;
        }
    }

    return column;
}

std::string flex_table_t::build_sql_prepare_get_wkb() const
{
    util::string_joiner_t joiner{',', '"'};
    for (auto const &column : m_columns) {
        if (!column.expire_configs().empty()) {
            joiner.add(column.name());
        }
    }

    assert(!joiner.empty());

    std::string const columns = joiner();

    if (has_multicolumn_id_index()) {
        return fmt::format(
            R"(SELECT {} FROM {} WHERE "{}" = $1::char(1) AND "{}" = $2::bigint)",
            columns, full_name(), m_columns[0].name(), m_columns[1].name());
    }

    return fmt::format(R"(SELECT {} FROM {} WHERE "{}" = $1::bigint)", columns,
                       full_name(), id_column_names());
}

namespace {

std::string partition_clause(partition_spec_t const &spec)
{
    if (!spec.enabled()) {
        return {};
    }
    return fmt::format(R"( PARTITION BY {} ("{}"))",
                       spec.method == "hash" ? "HASH" : "RANGE", spec.column);
}

char const *partition_name_format(std::string const &interval)
{
    if (interval == "day") {
        return "YYYY_MM_DD";
    }
    if (interval == "week") {
        return "IYYY_\"w\"IW";
    }
    if (interval == "year") {
        return "YYYY";
    }
    return "YYYY_MM";
}

} // anonymous namespace

std::vector<std::string>
flex_table_t::build_sql_create_partitions(partition_spec_t const &spec,
                                          std::string const &table_name,
                                          bool with_fixed) const
{
    std::vector<std::string> sqls;
    if (!spec.enabled()) {
        return sqls;
    }

    auto const parent = qualified_name(schema(), table_name);
    auto const tablespace = tablespace_clause(m_data_tablespace);

    if (spec.method == "hash") {
        if (with_fixed) {
            for (int i = 0; i < spec.count; ++i) {
                sqls.push_back(fmt::format(
                    "CREATE TABLE IF NOT EXISTS {} PARTITION OF {}"
                    " FOR VALUES WITH (MODULUS {}, REMAINDER {}){}",
                    qualified_name(schema(), fmt::format("{}_p{}", table_name, i)),
                    parent, spec.count, i, tablespace));
            }
        }
        return sqls;
    }

    if (with_fixed) {
        sqls.push_back(fmt::format(
            "CREATE TABLE IF NOT EXISTS {} PARTITION OF {} DEFAULT{}",
            qualified_name(schema(), table_name + "_default"), parent,
            tablespace));
    }

    sqls.push_back(fmt::format(
        R"(DO $osm2pgsql$
DECLARE
    s timestamp;
    part text;
BEGIN
    FOR i IN 0..1 LOOP
        s := date_trunc('{0}', localtimestamp) + i * interval '1 {0}';
        part := '{1}_p' || to_char(s, '{2}');
        BEGIN
            EXECUTE format('CREATE TABLE IF NOT EXISTS %I.%I PARTITION OF %s FOR VALUES FROM (%L) TO (%L){3}',
                           '{4}', part, '{5}', s, s + interval '1 {0}');
        EXCEPTION WHEN check_violation THEN
            RAISE WARNING 'Partition % not created: the default partition already holds rows in its range.', part;
        END;
    END LOOP;
END
$osm2pgsql$)",
        spec.interval, table_name, partition_name_format(spec.interval),
        tablespace, schema(), parent));

    return sqls;
}

std::string
flex_table_t::build_sql_create_table(table_type ttype,
                                     std::string const &table_name) const
{
    assert(!m_columns.empty());

    std::string sql =
        fmt::format("CREATE {} TABLE IF NOT EXISTS {} (",
                    ttype == table_type::interim ? "UNLOGGED" : "", table_name);

    util::string_joiner_t joiner{','};
    for (auto const &column : m_columns) {
        // create_only columns are only created in permanent, not in the
        // interim tables
        if (ttype == table_type::permanent || !column.create_only()) {
            joiner.add(column.sql_create());
        }
    }

    sql += joiner();
    sql += ')';

    if (ttype == table_type::permanent) {
        sql += partition_clause(m_partition);
    }

    if (ttype == table_type::interim) {
        sql += " WITH (autovacuum_enabled = off)";
    }

    sql += tablespace_clause(m_data_tablespace);

    return sql;
}

std::string flex_table_t::build_sql_create_history_table() const
{
    assert(!m_columns.empty());
    assert(m_has_history);

    std::string sql =
        fmt::format("CREATE TABLE IF NOT EXISTS {} (", full_history_name());

    util::string_joiner_t joiner{','};
    for (auto const &column : m_columns) {
        joiner.add(column.sql_create());
    }
    joiner.add(R"("valid_to" timestamptz)");
    joiner.add(R"("validity" tstzrange GENERATED ALWAYS AS)"
               R"( (tstzrange("valid_from", "valid_to")) STORED)");

    sql += joiner();
    sql += ')';
    sql += partition_clause(m_history_partition);
    sql += tablespace_clause(m_data_tablespace);

    return sql;
}

std::vector<std::string> flex_table_t::build_sql_create_history_indexes() const
{
    assert(m_has_history);

    auto const tablespace = tablespace_clause(index_tablespace());
    std::vector<std::string> sqls;

    if (has_geom_column()) {
        sqls.push_back(fmt::format(
            R"(CREATE INDEX ON {} USING GIST ("{}", "validity"){})",
            full_history_name(), geom_column().name(), tablespace));
    } else {
        sqls.push_back(
            fmt::format(R"(CREATE INDEX ON {} USING GIST ("validity"){})",
                        full_history_name(), tablespace));
    }

    if (has_id_column()) {
        sqls.push_back(fmt::format("CREATE INDEX ON {} USING BTREE ({}){}",
                                   full_history_name(), id_column_names(),
                                   tablespace));
    }

    return sqls;
}

void flex_table_t::note_replaced_at(osmium::Timestamp timestamp) const
{
    std::lock_guard<std::mutex> const guard{m_replaced_range->mutex};
    auto &range = *m_replaced_range;
    if (!range.first.valid() || timestamp < range.first) {
        range.first = timestamp;
    }
    if (!range.last.valid() || timestamp > range.last) {
        range.last = timestamp;
    }
}

std::pair<osmium::Timestamp, osmium::Timestamp>
flex_table_t::replaced_range() const
{
    std::lock_guard<std::mutex> const guard{m_replaced_range->mutex};
    return {m_replaced_range->first, m_replaced_range->last};
}

flex_table_t::history_comparison_t
flex_table_t::history_comparison() const
{
    history_comparison_t cmp;
    util::string_joiner_t ids{','};
    util::string_joiner_t hist{','};
    util::string_joiner_t live{','};

    for (auto const &column : m_columns) {
        if (column.type() == table_column_type::id_type ||
            column.type() == table_column_type::id_num) {
            if (!cmp.match.empty()) {
                cmp.match += " AND ";
                cmp.match_deleted += " AND ";
            }
            cmp.match += fmt::format(R"(h."{0}" = b."{0}")", column.name());
            cmp.match_deleted +=
                fmt::format(R"(d."{0}" = b."{0}")", column.name());
            ids.add(fmt::format(R"(h."{}")", column.name()));
            continue;
        }
        if (column.create_only() || column.name() == "valid_from") {
            continue;
        }
        hist.add(fmt::format(R"(h."{}")", column.name()));
        live.add(fmt::format(R"(b."{}")", column.name()));
    }

    cmp.ids = ids();
    cmp.hist = hist();
    cmp.live = live();
    return cmp;
}

std::string flex_table_t::build_sql_dedup_history(osmium::Timestamp first,
                                                  osmium::Timestamp last) const
{
    assert(m_has_history);

    auto const cmp = history_comparison();
    if (cmp.match.empty() || cmp.hist.empty()) {
        return {};
    }

    // A version superseded by an identical row was not a change. Its
    // history row, which ends exactly where the live row begins, is dropped
    // and the live row takes over its valid_from, so the timeline has no
    // gap. Only rows superseded in this run (by valid_to) are looked at.
    return fmt::format(
        R"(WITH d AS (DELETE FROM {0} h USING {1} b WHERE {2})"
        R"( AND h."valid_to" = b."valid_from")"
        R"( AND h."valid_to" BETWEEN '{3}' AND '{4}')"
        R"( AND ROW({5}) IS NOT DISTINCT FROM ROW({6}))"
        R"( RETURNING {7}, h."valid_from"))"
        R"( UPDATE {1} b SET "valid_from" = d."valid_from" FROM d WHERE {8})",
        full_history_name(), full_name(), cmp.match, first.to_iso(),
        last.to_iso(), cmp.hist, cmp.live, cmp.ids, cmp.match_deleted);
}

std::string
flex_table_t::build_sql_count_dropped_changes(osmium::Timestamp first,
                                              osmium::Timestamp last) const
{
    assert(m_has_history);

    auto const cmp = history_comparison();
    if (cmp.match.empty() || cmp.hist.empty()) {
        return {};
    }

    // A never-valid version that differs from the live row was replaced by
    // a different version with the same timestamp, for example a way
    // rebuilt because one of its nodes moved.
    return fmt::format(
        R"(SELECT count(*) FROM {0} h JOIN {1} b ON {2})"
        R"( WHERE h."valid_from" = h."valid_to")"
        R"( AND h."valid_to" BETWEEN '{3}' AND '{4}')"
        R"( AND ROW({5}) IS DISTINCT FROM ROW({6}))",
        full_history_name(), full_name(), cmp.match, first.to_iso(),
        last.to_iso(), cmp.hist, cmp.live);
}

std::string
flex_table_t::build_sql_delete_empty_history(osmium::Timestamp first,
                                             osmium::Timestamp last) const
{
    assert(m_has_history);

    // A version replaced by one with the same timestamp, such as a re-applied
    // change, was never valid at any time.
    return fmt::format(R"(DELETE FROM {} WHERE "valid_from" = "valid_to")"
                       R"( AND "valid_to" BETWEEN '{}' AND '{}')",
                       full_history_name(), first.to_iso(), last.to_iso());
}

std::string flex_table_t::build_sql_merge_history(osmium::Timestamp first,
                                                  osmium::Timestamp last) const
{
    assert(m_has_history);

    util::string_joiner_t ids{','};
    util::string_joiner_t values{','};
    util::string_joiner_t h_values{','};
    util::string_joiner_t select{','};
    std::string runs_match;
    std::string rows_match;

    for (auto const &column : m_columns) {
        if (column.create_only()) {
            continue;
        }
        if (column.name() == "valid_from") {
            select.add("d.merged_from");
            continue;
        }
        select.add(fmt::format(R"(d."{}")", column.name()));
        if (column.type() == table_column_type::id_type ||
            column.type() == table_column_type::id_num) {
            ids.add(fmt::format(R"("{}")", column.name()));
            runs_match += fmt::format(R"(u."{0}" = m."{0}" AND )", column.name());
            rows_match += fmt::format(R"(h."{0}" = u."{0}" AND )", column.name());
            continue;
        }
        values.add(fmt::format(R"("{}")", column.name()));
        h_values.add(fmt::format(R"(h."{}")", column.name()));
    }
    select.add("d.merged_to");

    if (runs_match.empty() || values.empty()) {
        return {};
    }

    // Rows of an object with the same values form a run as long as each one
    // starts where the one before ended. The rows of a run of several are
    // replaced by one row spanning all of them, as a version that changed
    // nothing was not a change.
    return fmt::format(
        FMT_STRING(
            R"(WITH v AS (SELECT {1}, ROW({2})::text AS row_key,)"
            R"( "valid_from", "valid_to",)"
            R"( CASE WHEN lag("valid_to") OVER w = "valid_from")"
            R"( THEN 0 ELSE 1 END AS starts_run FROM {0})"
            R"( WHERE "valid_to" BETWEEN '{3}' AND '{4}')"
            R"( WINDOW w AS (PARTITION BY {1}, ROW({2})::text)"
            R"( ORDER BY "valid_from")),)"
            R"( u AS (SELECT *, sum(starts_run) OVER (PARTITION BY {1}, row_key)"
            R"( ORDER BY "valid_from") AS run FROM v),)"
            R"( m AS (SELECT {1}, row_key, run, min("valid_from") AS merged_from,)"
            R"( max("valid_to") AS merged_to FROM u GROUP BY {1}, row_key, run)"
            R"( HAVING count(*) > 1),)"
            R"( d AS (DELETE FROM {0} h USING u JOIN m ON {5})"
            R"(u.row_key = m.row_key AND u.run = m.run)"
            R"( WHERE {6}ROW({7})::text = u.row_key)"
            R"( AND h."valid_from" = u."valid_from")"
            R"( AND h."valid_to" = u."valid_to")"
            R"( RETURNING h.*, m.row_key, m.merged_from, m.merged_to))"
            R"( INSERT INTO {0} ({8},"valid_to"))"
            R"( SELECT DISTINCT ON ({1}, d.merged_from, d.row_key) {9})"
            R"( FROM d)"),
        full_history_name(), ids(), values(), first.to_iso(), last.to_iso(),
        runs_match, rows_match, h_values(), build_sql_column_list(), select());
}

std::string flex_table_t::build_sql_column_list() const
{
    assert(!m_columns.empty());

    util::string_joiner_t joiner{',', '"'};

    for (auto const &column : m_columns) {
        if (!column.create_only()) {
            joiner.add(column.name());
        }
    }

    return joiner();
}

std::string flex_table_t::build_sql_create_id_index() const
{
    if (m_primary_key_index) {
        auto ts = tablespace_clause(index_tablespace());
        if (!ts.empty()) {
            ts = " USING INDEX" + ts;
        }
        return fmt::format("ALTER TABLE {} ADD PRIMARY KEY ({}){}", full_name(),
                           id_column_names(), ts);
    }

    return fmt::format("CREATE {}INDEX ON {} USING BTREE ({}) {}",
                       m_build_unique_id_index ? "UNIQUE " : "", full_name(),
                       id_column_names(),
                       tablespace_clause(index_tablespace()));
}

flex_index_t &flex_table_t::add_index(std::string method)
{
    return m_indexes.emplace_back(std::move(method));
}

bool flex_table_t::has_columns_with_expire() const noexcept
{
    return std::any_of(m_columns.cbegin(), m_columns.cend(),
                       [](auto const &column) { return column.has_expire(); });
}

void flex_table_t::prepare(pg_conn_t const &db_connection) const
{
    if (has_id_column() && has_columns_with_expire()) {
        auto const stmt = fmt::format("get_wkb_{}", m_table_num);
        db_connection.prepare(stmt, fmt::runtime(build_sql_prepare_get_wkb()));
    }
}

void flex_table_t::analyze(pg_conn_t const &db_connection) const
{
    analyze_table(db_connection, schema(), name());
}

void flex_table_t::enable_id_cache() noexcept { m_with_id_cache = true; }

bool flex_table_t::with_id_cache() const noexcept { return m_with_id_cache; }

namespace {

void enable_check_trigger(pg_conn_t const &db_connection,
                          flex_table_t const &table)
{
    std::string checks;

    for (auto const &column : table.columns()) {
        if (column.is_geometry_column() && column.needs_isvalid()) {
            checks.append(fmt::format(
                R"((NEW."{0}" IS NULL OR ST_IsValid(NEW."{0}")) AND )",
                column.name()));
        }
    }

    if (checks.empty()) {
        return;
    }

    // remove last " AND "
    checks.resize(checks.size() - 5);

    create_geom_check_trigger(db_connection, table.schema(), table.name(),
                              checks);
}

} // anonymous namespace

void table_connection_t::start(pg_conn_t const &db_connection,
                               bool append) const
{
    if (!append) {
        drop_table_if_exists(db_connection, table().schema(), table().name());
        if (table().has_history()) {
            drop_table_if_exists(db_connection, table().schema(),
                                 table().history_name());
        }
    }

    // These _tmp tables can be left behind if we run out of disk space.
    drop_table_if_exists(db_connection, table().schema(),
                         table().name() + "_tmp");

    if (!append) {
        db_connection.exec(table().build_sql_create_table(
            table().cluster_by_geom() ? flex_table_t::table_type::interim
                                      : flex_table_t::table_type::permanent,
            table().full_name()));

        if (!table().cluster_by_geom()) {
            for (auto const &sql : table().build_sql_create_partitions(
                     table().partition(), table().name(), true)) {
                db_connection.exec(sql);
            }
        }

        enable_check_trigger(db_connection, table());

        if (table().has_history()) {
            db_connection.exec(table().build_sql_create_history_table());
            for (auto const &sql : table().build_sql_create_partitions(
                     table().history_partition(), table().history_name(),
                     true)) {
                db_connection.exec(sql);
            }
            for (auto const &sql : table().build_sql_create_history_indexes()) {
                db_connection.exec(sql);
            }
        }
    } else {
        for (auto const &sql : table().build_sql_create_partitions(
                 table().partition(), table().name(), false)) {
            db_connection.exec(sql);
        }
        if (table().has_history()) {
            for (auto const &sql : table().build_sql_create_partitions(
                     table().history_partition(), table().history_name(),
                     false)) {
                db_connection.exec(sql);
            }
        }
    }

    table().prepare(db_connection);
}

void table_connection_t::warn_about_dropped_changes(
    pg_conn_t const &db_connection, osmium::Timestamp first,
    osmium::Timestamp last) const
{
    auto const sql = table().build_sql_count_dropped_changes(first, last);
    if (sql.empty()) {
        return;
    }

    auto const result = db_connection.exec(sql);
    auto const count = std::strtoull(result.get_value(0, 0), nullptr, 10);
    if (count > 0) {
        log_warn("{} version(s) of table '{}' were replaced by a different"
                 " version with the same timestamp and are not kept in"
                 " history. This happens when a node changes without the way"
                 " that uses it, or when an older change is applied again.",
                 count, table().name());
    }
}

void table_connection_t::clean_up_history(pg_conn_t const &db_connection,
                                          bool merge) const
{
    auto const [first, last] = table().replaced_range();
    if (!table().has_history() || !table().has_id_column() || !first.valid()) {
        return;
    }

    if (merge) {
        log_info("Merging history of table '{}'...", table().name());
        auto const sql = table().build_sql_merge_history(first, last);
        if (!sql.empty()) {
            db_connection.exec(sql);
        }
    }

    warn_about_dropped_changes(db_connection, first, last);
    db_connection.exec(table().build_sql_delete_empty_history(first, last));
    auto const sql = table().build_sql_dedup_history(first, last);
    if (!sql.empty()) {
        db_connection.exec(sql);
    }
}

void table_connection_t::stop(pg_conn_t const &db_connection, bool updateable,
                              bool append)
{
    sync();

    if (append) {
        clean_up_history(db_connection, false);
        return;
    }

    if (table().cluster_by_geom()) {
        if (table().geom_column().needs_isvalid()) {
            drop_geom_check_trigger(db_connection, table().schema(),
                                    table().name());
        }

        log_info("Clustering table '{}' by geometry...", table().name());

        std::string const columns = table().build_sql_column_list();

        auto const geom_column_name =
            "\"" + table().geom_column().name() + "\"";

        if (table().partition().enabled()) {
            db_connection.exec(R"(ALTER TABLE {} RENAME TO "{}_tmp")",
                               table().full_name(), table().name());
            db_connection.exec(table().build_sql_create_table(
                flex_table_t::table_type::permanent, table().full_name()));
            for (auto const &sql : table().build_sql_create_partitions(
                     table().partition(), table().name(), true)) {
                db_connection.exec(sql);
            }
            db_connection.exec(fmt::format(
                "INSERT INTO {} ({}) SELECT {} FROM {} ORDER BY {}",
                table().full_name(), columns, columns, table().full_tmp_name(),
                geom_column_name));
            db_connection.exec("DROP TABLE {}", table().full_tmp_name());
        } else {
            db_connection.exec(table().build_sql_create_table(
                flex_table_t::table_type::permanent, table().full_tmp_name()));

            std::string const sql =
                fmt::format("INSERT INTO {} ({}) SELECT {} FROM {} ORDER BY {}",
                            table().full_tmp_name(), columns, columns,
                            table().full_name(), geom_column_name);

            db_connection.exec(sql);

            db_connection.exec("DROP TABLE {}", table().full_name());
            db_connection.exec(R"(ALTER TABLE {} RENAME TO "{}")",
                               table().full_tmp_name(), table().name());
        }
        m_id_index_created = false;

        if (updateable) {
            enable_check_trigger(db_connection, table());
        }
    }

    clean_up_history(db_connection, true);

    if (table().indexes().empty()) {
        log_info("No indexes to create on table '{}'.", table().name());
    } else {
        for (auto const &index : table().indexes()) {
            log_info("Creating index on table '{}' {}...", table().name(),
                     index.columns());
            auto const sql = index.create_index(
                qualified_name(table().schema(), table().name()));
            db_connection.exec(sql);
        }
    }

    if ((table().always_build_id_index() || updateable) &&
        table().has_id_column()) {
        create_id_index(db_connection);
    }

    log_info("Analyzing table '{}'...", table().name());
    table().analyze(db_connection);
}

void table_connection_t::create_id_index(pg_conn_t const &db_connection)
{
    if (m_id_index_created) {
        log_debug("Id index on table '{}' already created.", table().name());
    } else {
        log_info("Creating id index on table '{}'...", table().name());
        db_connection.exec(table().build_sql_create_id_index());
        m_id_index_created = true;
    }
}

pg_result_t table_connection_t::get_geoms_by_id(pg_conn_t const &db_connection,
                                                osmium::item_type type,
                                                osmid_t id) const
{
    assert(table().has_geom_column());
    std::string const stmt = fmt::format("get_wkb_{}", table().num());
    if (table().has_multicolumn_id_index()) {
        return db_connection.exec_prepared_as_binary(stmt.c_str(),
                                                     type_to_char(type), id);
    }
    return db_connection.exec_prepared_as_binary(stmt.c_str(), id);
}

void table_connection_t::delete_rows_with(osmium::item_type type, osmid_t id,
                                          osmium::Timestamp timestamp)
{
    m_copy_mgr.new_line(m_target);

    if (!table().has_multicolumn_id_index()) {
        type = osmium::item_type::undefined;
    }
    m_copy_mgr.delete_object(type_to_char(type)[0], id, timestamp);

    if (table().has_history() && timestamp.valid()) {
        table().note_replaced_at(timestamp);
    }
}

void table_connection_t::task_wait()
{
    auto const run_time = m_task_result.wait();
    log_info("All postprocessing on table '{}' done in {}.", table().name(),
             util::human_readable_duration(run_time));
    log_debug("Inserted {} rows into table '{}' ({} not inserted due to"
              " NOT NULL constraints).",
              m_count_insert, table().name(), m_count_not_null_error);
}
