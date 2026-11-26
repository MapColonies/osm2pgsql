Feature: History needs object attributes

    History intervals are measured in the source's own timestamps, which
    osm2pgsql only reads with --extra-attributes.

    Background:
        Given the OSM data
            """
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            """

    Scenario: --history without --extra-attributes fails before touching the database
        Given the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'point', projection = 4326 },
            })

            function osm2pgsql.process_node(object)
                points:insert({ name = object.tags.name, geom = object:as_point() })
            end
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | --history |
        Then execution fails
        And the error output contains
            """
            History needs the timestamps of the source's versions. Use -x/--extra-attributes.
            """
        And there are no tables osm2pgsql_test_points
        And there are no tables planet_osm_nodes, planet_osm_ways, planet_osm_rels

    Scenario: A table with history in the Lua, without --extra-attributes, fails
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
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then execution fails
        And the error output contains
            """
            Table 'osm2pgsql_test_points' has history enabled. History needs the timestamps of the source's versions. Use -x/--extra-attributes.
            """
        And there are no tables osm2pgsql_test_points
        And table planet_osm_nodes is empty
        And table planet_osm_ways is empty
        And table planet_osm_rels is empty

    Scenario: An append inherits --history and --extra-attributes from the import
        Given the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'point', projection = 4326 },
            })

            function osm2pgsql.process_node(object)
                points:insert({ name = object.tags.name, geom = object:as_point() })
            end
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | --history | -x |
        Then execution is successful

        Given the OSM data
            """
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And the error output contains
            """
            Updating with attributes (same as on import).
            """
        And the error output contains
            """
            Updating with history (same as on import).
            """
        And table osm2pgsql_test_points_history contains exactly
            | node_id | name |
            | 10      | one  |

    Scenario: An append to a history database recorded without attributes fails
        Given the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'point', projection = 4326 },
            }, { history = false })

            function osm2pgsql.process_node(object)
                points:insert({ name = object.tags.name, geom = object:as_point() })
            end
            """
        And the SQL statement forget_attributes
            """
            UPDATE osm2pgsql_properties SET value = 'false'
             WHERE property = 'attributes'
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | --history | -x |
        Then execution is successful

        When executing the SQL statement forget_attributes

        Given the OSM data
            """
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution fails
        And the error output contains
            """
            History needs the timestamps of the source's versions. Use -x/--extra-attributes.
            """

    Scenario: A version without a timestamp in a history table fails
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
        And the OSM data
            """
            n11 v1 dV Tname=two x11.0 y11.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x |
        Then execution fails
        And the error output contains
            """
            Node 11 has no timestamp. History needs the timestamps of the source's versions.
            """
