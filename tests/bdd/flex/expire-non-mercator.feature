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
        And the file expire.list contains exactly
            """
            12/2161/1933
            12/2167/1927
            """

    Scenario Outline: Tables in EPSG:3857 and EPSG:4326 share command line expire
        Given the lua style
            """
            local first = osm2pgsql.define_node_table('osm2pgsql_test_first', {
                { column = 'geom', type = 'point', projection = <first> },
            })

            local second = osm2pgsql.define_node_table('osm2pgsql_test_second', {
                { column = 'geom', type = 'point', projection = <second> },
            })

            function osm2pgsql.process_node(object)
                if object.tags.first then
                    first:insert({ geom = object:as_point() })
                else
                    second:insert({ geom = object:as_point() })
                end
            end
            """
        And the OSM data
            """
            n10 v1 dV Tfirst=yes x10.0 y10.0
            n11 v1 dV Tsecond=yes x-10.0 y-10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then execution is successful

        Given the OSM data
            """
            n10 v2 dV Tfirst=yes x10.5 y10.5
            n11 v2 dV Tsecond=yes x-10.5 y-10.5
            """
        When running osm2pgsql flex with parameters
            | --slim | -a | --expire-tiles=12 | --expire-output=expire.list |
        Then execution is successful
        And the file expire.list contains exactly
            """
            12/2161/1933
            12/2167/1927
            12/1934/2162
            12/1928/2168
            """

        Examples:
            | first | second |
            | 3857  | 4326   |
            | 4326  | 3857   |

    @config.have_proj
    Scenario: Tables in other projections are not expired
        Given the lua style
            """
            local merc = osm2pgsql.define_node_table('osm2pgsql_test_merc', {
                { column = 'geom', type = 'point', projection = 3857 },
            })

            local itm = osm2pgsql.define_node_table('osm2pgsql_test_itm', {
                { column = 'geom', type = 'point', projection = 2039 },
            })

            function osm2pgsql.process_node(object)
                if object.tags.merc then
                    merc:insert({ geom = object:as_point() })
                else
                    itm:insert({ geom = object:as_point() })
                end
            end
            """
        And the OSM data
            """
            n10 v1 dV Tmerc=yes x10.0 y10.0
            n11 v1 dV Titm=yes x35.0 y32.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then execution is successful

        Given the OSM data
            """
            n10 v2 dV Tmerc=yes x10.5 y10.5
            n11 v2 dV Titm=yes x35.1 y32.1
            """
        When running osm2pgsql flex with parameters
            | --slim | -a | --expire-tiles=12 | --expire-output=expire.list |
        Then execution is successful
        And the file expire.list contains exactly
            """
            12/2161/1933
            12/2167/1927
            """
