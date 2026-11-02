Feature: Expire with command line options on geometries not in Web Mercator

    Scenario: Tables in EPSG:4326 are expired
        Given the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'geom', type = 'point', projection = 4326 },
            })

            function osm2pgsql.process_node(object)
                points:insert({ geom = object:as_point() })
            end
            """
        And the OSM data
            """
            n10 v1 dV Tamenity=restaurant x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then execution is successful

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

    Scenario: Tables in different projections can not share command line expire
        Given the lua style
            """
            local merc = osm2pgsql.define_node_table('osm2pgsql_test_merc', {
                { column = 'geom', type = 'point', projection = 3857 },
            })

            local latlon = osm2pgsql.define_node_table('osm2pgsql_test_latlon', {
                { column = 'geom', type = 'point', projection = 4326 },
            })

            function osm2pgsql.process_node(object)
                merc:insert({ geom = object:as_point() })
                latlon:insert({ geom = object:as_point() })
            end
            """
        And the OSM data
            """
            n10 v1 dV Tamenity=restaurant x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then execution is successful

        Given the OSM data
            """
            n10 v2 dV Tamenity=restaurant x10.5 y10.5
            """
        When running osm2pgsql flex with parameters
            | --slim | -a | --expire-tiles=12 |
        Then execution fails
        And the error output contains
            """
            Tile expiry needs all tables with a geometry column to use the same projection
            """
