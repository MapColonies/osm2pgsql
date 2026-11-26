Feature: Create with --history fills point history from earlier versions

    Every node version before the latest runs through process_node. Rows
    for tables with history go to their history tables, valid from the
    version's timestamp until the next version that changed the row.

    Background:
        Given the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'point', projection = 4326 },
            }, { history = true })

            local plain = osm2pgsql.define_node_table('osm2pgsql_test_plain', {
                { column = 'name', type = 'text' },
            })

            function osm2pgsql.process_node(object)
                if object.tags.name then
                    points:insert({ name = object.tags.name, geom = object:as_point() })
                    plain:insert({ name = object.tags.name })
                end
            end
            """
        And the SQL statement live_rows
            """
            SELECT node_id, name, ST_AsText(geom) AS geom,
                   to_char(valid_from AT TIME ZONE 'UTC', 'YYYY-MM-DD') AS valid_from
              FROM osm2pgsql_test_points
            """
        And the SQL statement history_rows
            """
            SELECT node_id, name, ST_AsText(geom) AS geom,
                   to_char(valid_from AT TIME ZONE 'UTC', 'YYYY-MM-DD') AS valid_from,
                   to_char(valid_to AT TIME ZONE 'UTC', 'YYYY-MM-DD') AS valid_to
              FROM osm2pgsql_test_points_history
            """

    Scenario: Earlier versions of a node become history rows
        Given the OSM data
            """
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            n10 v3 dV t2024-03-01T00:00:00Z Tname=two x11.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name | geom         | valid_from | valid_to   |
            | 10      | one  | POINT(10 10) | 2024-01-01 | 2024-02-01 |
            | 10      | two  | POINT(10 10) | 2024-02-01 | 2024-03-01 |
        And statement live_rows returns exactly
            | node_id | name | geom         | valid_from |
            | 10      | two  | POINT(11 10) | 2024-03-01 |

    Scenario: A version that changes nothing the Lua writes adds no history row
        Given the OSM data
            """
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            n10 v2 dV t2024-02-01T00:00:00Z Tname=one,note=x x10.0 y10.0
            n10 v3 dV t2024-03-01T00:00:00Z Tname=two x10.0 y10.0
            n10 v4 dV t2024-04-01T00:00:00Z Tname=two,note=y x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name | geom         | valid_from | valid_to   |
            | 10      | one  | POINT(10 10) | 2024-01-01 | 2024-03-01 |
        And statement live_rows returns exactly
            | node_id | name | geom         | valid_from |
            | 10      | two  | POINT(10 10) | 2024-03-01 |

    Scenario: A version that writes no row ends the previous row
        Given the OSM data
            """
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            n10 v2 dV t2024-02-01T00:00:00Z x10.0 y10.0
            n10 v3 dV t2024-03-01T00:00:00Z Tname=one x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name | geom         | valid_from | valid_to   |
            | 10      | one  | POINT(10 10) | 2024-01-01 | 2024-02-01 |
        And statement live_rows returns exactly
            | node_id | name | geom         | valid_from |
            | 10      | one  | POINT(10 10) | 2024-03-01 |

    Scenario: A deleted node has only history rows, the last closed at the deletion
        Given the OSM data
            """
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            n10 v3 dD t2024-03-01T00:00:00Z x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name | geom         | valid_from | valid_to   |
            | 10      | one  | POINT(10 10) | 2024-01-01 | 2024-02-01 |
            | 10      | two  | POINT(10 10) | 2024-02-01 | 2024-03-01 |
        And table osm2pgsql_test_points has 0 rows

    Scenario: A table without history gets nothing from earlier versions
        Given the OSM data
            """
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            n11 v1 dV t2024-01-01T00:00:00Z Tname=gone x11.0 y11.0
            n11 v2 dD t2024-02-01T00:00:00Z x11.0 y11.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And table osm2pgsql_test_plain contains exactly
            | node_id | name |
            | 10      | two  |

    Scenario: An append after the backfill closes the live row at the new version
        Given the OSM data
            """
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful

        Given the OSM data
            """
            n10 v3 dV t2024-03-01T00:00:00Z Tname=three x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name | geom         | valid_from | valid_to   |
            | 10      | one  | POINT(10 10) | 2024-01-01 | 2024-02-01 |
            | 10      | two  | POINT(10 10) | 2024-02-01 | 2024-03-01 |
        And statement live_rows returns exactly
            | node_id | name  | geom         | valid_from |
            | 10      | three | POINT(10 10) | 2024-03-01 |
