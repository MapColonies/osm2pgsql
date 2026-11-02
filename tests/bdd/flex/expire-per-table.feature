Feature: Command line expire writes a list per table

    Background:
        Given the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'geom', type = 'point', projection = 4326 },
            })

            local helper = osm2pgsql.define_node_table('osm2pgsql_test_helper', {
                { column = 'geom', type = 'point', projection = 4326 },
            }, { expire = false })

            function osm2pgsql.process_node(object)
                points:insert({ geom = object:as_point() })
                helper:insert({ geom = object:as_point() })
            end
            """
        And the OSM data
            """
            n10 v1 dV Tamenity=restaurant x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then execution is successful

    Scenario: Each expiring table gets its own expire output
        Given the OSM data
            """
            n10 v2 dV Tamenity=restaurant x10.5 y10.5
            """
        When running osm2pgsql flex with parameters
            | --slim | -a | --expire-tiles=12 | --expire-output=expire.list |
        Then execution is successful
        And the error output contains
            """
            entries to expire output [0].
            """
        And the error output contains
            """
            entries to expire output [1].
            """

    Scenario: A table with expire = false gets no expire output
        Given the OSM data
            """
            n10 v2 dV Tamenity=restaurant x10.5 y10.5
            """
        When running osm2pgsql flex with parameters
            | --slim | -a | --expire-tiles=12 | --expire-output=expire.list |
        Then execution is successful
        And the error output does not contain
            """
            entries to expire output [2].
            """

    Scenario: The expire table option must be a boolean
        Given the lua style
            """
            osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'geom', type = 'point' },
            }, { expire = 'no' })
            """
        And the OSM data
            """
            n10 v1 dV x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | -c |
        Then execution fails
        And the error output contains
            """
            The table field 'expire' must be a boolean field.
            """
