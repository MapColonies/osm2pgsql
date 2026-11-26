Feature: History intervals are measured in source time

    A feature version is valid from the timestamp of the source version that
    produced it until the timestamp of the version that superseded it.

    Background:
        Given the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'point', projection = 4326 },
            }, { history = true })

            function osm2pgsql.process_node(object)
                points:insert({ name = object.tags.name, geom = object:as_point() })
            end
            """
        And the SQL statement live_rows
            """
            SELECT node_id, name, to_char(valid_from AT TIME ZONE 'UTC', 'YYYY-MM-DD') AS valid_from
              FROM osm2pgsql_test_points
            """
        And the SQL statement history_rows
            """
            SELECT node_id, name,
                   to_char(valid_from AT TIME ZONE 'UTC', 'YYYY-MM-DD') AS valid_from,
                   to_char(valid_to AT TIME ZONE 'UTC', 'YYYY-MM-DD') AS valid_to
              FROM osm2pgsql_test_points_history
            """
        And the OSM data
            """
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x |
        Then execution is successful

    Scenario: A live row is valid from its version's timestamp
        Then statement live_rows returns exactly
            | node_id | name | valid_from |
            | 10      | one  | 2024-01-01 |

    Scenario: A modify closes the previous version at the new version's timestamp
        Given the OSM data
            """
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name | valid_from | valid_to   |
            | 10      | one  | 2024-01-01 | 2024-02-01 |
        And statement live_rows returns exactly
            | node_id | name | valid_from |
            | 10      | two  | 2024-02-01 |

    Scenario: A delete closes the last version at the deletion's timestamp
        Given the OSM data
            """
            n10 v2 dD t2024-03-01T00:00:00Z x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name | valid_from | valid_to   |
            | 10      | one  | 2024-01-01 | 2024-03-01 |
        And table osm2pgsql_test_points has 0 rows

    Scenario: A version that changes nothing leaves history unchanged
        Given the OSM data
            """
            n10 v2 dV t2024-02-01T00:00:00Z Tname=one x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And table osm2pgsql_test_points_history has 0 rows
        And statement live_rows returns exactly
            | node_id | name | valid_from |
            | 10      | one  | 2024-01-01 |

    Scenario: A diff applied twice leaves history unchanged
        Given the OSM data
            """
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful

        Given the OSM data
            """
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name | valid_from | valid_to   |
            | 10      | one  | 2024-01-01 | 2024-02-01 |
        And statement live_rows returns exactly
            | node_id | name | valid_from |
            | 10      | two  | 2024-02-01 |

    Scenario: A feature's intervals meet exactly across several changes
        Given the OSM data
            """
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful

        Given the OSM data
            """
            n10 v3 dV t2024-03-01T00:00:00Z Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful

        Given the OSM data
            """
            n10 v4 dV t2024-04-01T00:00:00Z Tname=four x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name | valid_from | valid_to   |
            | 10      | one  | 2024-01-01 | 2024-02-01 |
            | 10      | two  | 2024-02-01 | 2024-04-01 |
        And statement live_rows returns exactly
            | node_id | name | valid_from |
            | 10      | four | 2024-04-01 |

    Scenario: An older change applied after a newer one does not fail the run
        Given the OSM data
            """
            n10 v3 dV t2024-03-01T00:00:00Z Tname=three x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful

        Given the OSM data
            """
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And statement live_rows returns exactly
            | node_id | name | valid_from |
            | 10      | two  | 2024-02-01 |
