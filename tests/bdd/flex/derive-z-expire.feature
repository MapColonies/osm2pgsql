Feature: Expire tiles on tables with a derived Z geometry

    Scenario: Updating a Z geometry with tile expiry on
        Given the SQL statement way_geoms
            """
            SELECT ST_AsText(geom) AS wkt FROM osm2pgsql_test_ways
            """
        And the lua style
            """
            local ways = osm2pgsql.define_way_table('osm2pgsql_test_ways', {
                { column = 'geom', type = 'linestring', projection = 4326,
                  derive_z_from = 'altitude' },
            })

            function osm2pgsql.process_way(object)
                ways:insert({ geom = object:as_linestring() })
            end
            """
        And the OSM data
            """
            n1 v1 dV Taltitude=10 x10.0 y10.0
            n2 v1 dV Taltitude=20 x10.1 y10.1
            w10 v1 dV Thighway=primary Nn1,n2
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then execution is successful
        And statement way_geoms returns exactly
            | wkt |
            | LINESTRING Z (10 10 10,10.1 10.1 20) |

        Given the OSM data
            """
            n2 v2 dV Taltitude=30 x10.1 y10.1
            """
        When running osm2pgsql flex with parameters
            | --slim | -a | --expire-tiles=12 | --expire-output=expire.list |
        Then execution is successful
        And the error output contains
            """
            entries to expire output [0].
            """
        And statement way_geoms returns exactly
            | wkt |
            | LINESTRING Z (10 10 10,10.1 10.1 30) |
