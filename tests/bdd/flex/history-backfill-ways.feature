Feature: Create with --history fills way history from earlier versions

    Every way version before the latest runs through process_way, with the
    geometry built from each node's version current at the way version's
    timestamp.

    Background:
        Given the lua style
            """
            local polygons = osm2pgsql.define_way_table('osm2pgsql_test_polygons', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'polygon', projection = 4326 },
            }, { history = true })

            function osm2pgsql.process_way(object)
                if object.tags.building then
                    polygons:insert({ name = object.tags.name, geom = object:as_polygon() })
                end
            end
            """
        And the SQL statement live_rows
            """
            SELECT way_id, name, ST_AsText(geom) AS geom,
                   to_char(valid_from AT TIME ZONE 'UTC', 'YYYY-MM-DD') AS valid_from
              FROM osm2pgsql_test_polygons
            """
        And the SQL statement history_rows
            """
            SELECT way_id, name, ST_AsText(geom) AS geom,
                   to_char(valid_from AT TIME ZONE 'UTC', 'YYYY-MM-DD') AS valid_from,
                   to_char(valid_to AT TIME ZONE 'UTC', 'YYYY-MM-DD') AS valid_to
              FROM osm2pgsql_test_polygons_history
            """

    Scenario: Earlier versions of a polygon keep the geometry of their own time
        Given the OSM data
            """
            n1 v1 dV t2024-01-01T00:00:00Z x0 y0
            n2 v1 dV t2024-01-01T00:00:00Z x1 y0
            n3 v1 dV t2024-01-01T00:00:00Z x1 y1
            n3 v2 dV t2024-02-01T00:00:00Z x2 y2
            n4 v1 dV t2024-01-01T00:00:00Z x0 y1
            w20 v1 dV t2024-01-01T00:00:00Z Tbuilding=yes,name=a Nn1,n2,n3,n4,n1
            w20 v2 dV t2024-02-01T00:00:00Z Tbuilding=yes,name=a Nn1,n2,n3,n4,n1
            w20 v3 dV t2024-03-01T00:00:00Z Tbuilding=yes,name=b Nn1,n2,n3,n4,n1
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And statement history_rows returns exactly
            | way_id | name | geom                           | valid_from | valid_to   |
            | 20     | a    | POLYGON((0 0,1 0,1 1,0 1,0 0)) | 2024-01-01 | 2024-02-01 |
            | 20     | a    | POLYGON((0 0,1 0,2 2,0 1,0 0)) | 2024-02-01 | 2024-03-01 |
        And statement live_rows returns exactly
            | way_id | name | geom                           | valid_from |
            | 20     | b    | POLYGON((0 0,1 0,2 2,0 1,0 0)) | 2024-03-01 |

    Scenario: A node reused at a new position leaves earlier geometry unchanged
        Given the OSM data
            """
            n1 v1 dV t2024-01-01T00:00:00Z x0 y0
            n2 v1 dV t2024-01-01T00:00:00Z x1 y0
            n3 v1 dV t2024-01-01T00:00:00Z x1 y1
            n3 v2 dV t2024-02-01T00:00:00Z x5 y5
            n4 v1 dV t2024-01-01T00:00:00Z x0 y1
            n5 v1 dV t2024-02-01T00:00:00Z x6 y5
            n6 v1 dV t2024-02-01T00:00:00Z x6 y6
            w20 v1 dV t2024-01-01T00:00:00Z Tbuilding=yes,name=a Nn1,n2,n3,n4,n1
            w20 v2 dV t2024-02-01T00:00:00Z Tbuilding=yes,name=a Nn1,n2,n4,n1
            w21 v1 dV t2024-02-01T00:00:00Z Tbuilding=yes,name=c Nn3,n5,n6,n3
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And statement history_rows returns exactly
            | way_id | name | geom                           | valid_from | valid_to   |
            | 20     | a    | POLYGON((0 0,1 0,1 1,0 1,0 0)) | 2024-01-01 | 2024-02-01 |
        And statement live_rows returns exactly
            | way_id | name | geom                         | valid_from |
            | 20     | a    | POLYGON((0 0,1 0,0 1,0 0))   | 2024-02-01 |
            | 21     | c    | POLYGON((5 5,6 5,6 6,5 5))   | 2024-02-01 |

    Scenario: A deleted way has only history rows
        Given the OSM data
            """
            n1 v1 dV t2024-01-01T00:00:00Z x0 y0
            n2 v1 dV t2024-01-01T00:00:00Z x1 y0
            n3 v1 dV t2024-01-01T00:00:00Z x1 y1
            w20 v1 dV t2024-01-01T00:00:00Z Tbuilding=yes,name=a Nn1,n2,n3,n1
            w20 v2 dV t2024-02-01T00:00:00Z Tbuilding=yes,name=b Nn1,n2,n3,n1
            w20 v3 dD t2024-03-01T00:00:00Z
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And statement history_rows returns exactly
            | way_id | name | geom                       | valid_from | valid_to   |
            | 20     | a    | POLYGON((0 0,1 0,1 1,0 0)) | 2024-01-01 | 2024-02-01 |
            | 20     | b    | POLYGON((0 0,1 0,1 1,0 0)) | 2024-02-01 | 2024-03-01 |
        And table osm2pgsql_test_polygons has 0 rows

    Scenario: A node moved without a new version of its way logs a warning
        # n2 v2 only changes tags, which changes no way. n10 is not a way
        # node.
        Given the OSM data
            """
            n1 v1 dV t2024-01-01T00:00:00Z x0 y0
            n2 v1 dV t2024-01-01T00:00:00Z x1 y0
            n2 v2 dV t2024-01-15T00:00:00Z Tentrance=yes x1 y0
            n3 v1 dV t2024-01-01T00:00:00Z x1 y1
            n3 v2 dV t2024-02-01T00:00:00Z x2 y2
            n10 v1 dV t2024-01-01T00:00:00Z Tamenity=bench x9 y9
            n10 v2 dV t2024-02-01T00:00:00Z Tamenity=table x9 y9
            w20 v1 dV t2024-01-01T00:00:00Z Tbuilding=yes,name=a Nn1,n2,n3,n1
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And the error output contains
            """
            1 node version(s) moved without a new version of a way using the node. History does not record these changes.
            """

    Scenario: No node version table is left after the create
        Given the OSM data
            """
            n1 v1 dV t2024-01-01T00:00:00Z x0 y0
            n2 v1 dV t2024-01-01T00:00:00Z x1 y0
            n3 v1 dV t2024-01-01T00:00:00Z x1 y1
            w20 v1 dV t2024-01-01T00:00:00Z Tbuilding=yes,name=a Nn1,n2,n3,n1
            w20 v2 dV t2024-02-01T00:00:00Z Tbuilding=yes,name=b Nn1,n2,n3,n1
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And there are no tables planet_osm_node_versions
