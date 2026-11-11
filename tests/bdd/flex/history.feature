Feature: History tables keep superseded versions

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
        And the SQL statement history_rows
            """
            SELECT node_id, name FROM osm2pgsql_test_points_history
            """
        And the SQL statement timeline_contiguous
            """
            SELECT bool_and(h.valid_to = b.valid_from) AS contiguous
              FROM osm2pgsql_test_points_history h
              JOIN osm2pgsql_test_points b USING (node_id)
            """
        And the OSM data
            """
            n10 v1 dV Tname=one x10.0 y10.0
            n11 v1 dV Tname=other x11.0 y11.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then execution is successful

    Scenario: A changed row moves into history and the timeline has no gap
        Given the OSM data
            """
            n10 v2 dV Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name |
            | 10      | one  |
        And statement timeline_contiguous returns exactly
            | contiguous |
            | True       |

    Scenario: An unchanged rewrite leaves no history row and no gap
        Given the OSM data
            """
            n10 v2 dV Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful

        Given the OSM data
            """
            n10 v3 dV Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name |
            | 10      | one  |
        And statement timeline_contiguous returns exactly
            | contiguous |
            | True       |

    Scenario: A deleted row moves into history
        Given the OSM data
            """
            n11 v2 dD x11.0 y11.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And statement history_rows returns exactly
            | node_id | name  |
            | 11      | other |
        And table osm2pgsql_test_points contains exactly
            | node_id |
            | 10      |

    Scenario: An unchanged rewrite of a derived Z geometry leaves no history row
        Given the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'point', projection = 4326,
                  derive_z_from = 'altitude' },
            }, { history = true })

            function osm2pgsql.process_node(object)
                points:insert({ name = object.tags.name, geom = object:as_point() })
            end
            """
        And the OSM data
            """
            n10 v1 dV Tname=one,altitude=10 x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then execution is successful

        Given the OSM data
            """
            n10 v2 dV Tname=one,altitude=10 x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And table osm2pgsql_test_points_history has 0 rows
