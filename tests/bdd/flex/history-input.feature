Feature: Create with --history reads several versions of an object

    A history file holds every version of an object, sorted by type, id and
    version. A create with --history builds the live and middle tables from
    the latest version of each object.

    Background:
        Given the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'point', projection = 4326 },
            }, { history = true })

            local lines = osm2pgsql.define_way_table('osm2pgsql_test_lines', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'linestring', projection = 4326 },
            }, { history = true })

            function osm2pgsql.process_node(object)
                if object.tags.name then
                    points:insert({ name = object.tags.name, geom = object:as_point() })
                end
            end

            function osm2pgsql.process_way(object)
                lines:insert({ name = object.tags.name, geom = object:as_linestring() })
            end
            """
        And the SQL statement points
            """
            SELECT node_id, name, ST_AsText(geom) AS geom FROM osm2pgsql_test_points
            """
        And the SQL statement lines
            """
            SELECT way_id, name, ST_AsText(geom) AS geom FROM osm2pgsql_test_lines
            """

    Scenario: The live tables hold the latest version of each object
        Given the OSM data
            """
            n1 v1 dV t2024-01-01T00:00:00Z x1.0 y1.0
            n1 v2 dV t2024-02-01T00:00:00Z x1.5 y1.0
            n2 v1 dV t2024-01-01T00:00:00Z x2.0 y2.0
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            n10 v3 dV t2024-03-01T00:00:00Z Tname=three x11.0 y10.0
            w20 v1 dV t2024-01-01T00:00:00Z Tname=road Nn1,n2
            w20 v2 dV t2024-02-01T00:00:00Z Tname=street Nn1,n2
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And statement points returns exactly
            | node_id | name  | geom          |
            | 10      | three | POINT(11 10)  |
        And statement lines returns exactly
            | way_id | name   | geom                  |
            | 20     | street | LINESTRING(1.5 1,2 2) |

    Scenario: An object whose latest version is deleted has no live row
        Given the OSM data
            """
            n1 v1 dV t2024-01-01T00:00:00Z x1.0 y1.0
            n2 v1 dV t2024-01-01T00:00:00Z x2.0 y2.0
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            n10 v2 dD t2024-02-01T00:00:00Z x10.0 y10.0
            n11 v1 dV t2024-01-01T00:00:00Z Tname=kept x11.0 y11.0
            w20 v1 dV t2024-01-01T00:00:00Z Tname=road Nn1,n2
            w20 v2 dD t2024-02-01T00:00:00Z
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And statement points returns exactly
            | node_id | name | geom         |
            | 11      | kept | POINT(11 11) |
        And table osm2pgsql_test_lines has 0 rows

    Scenario: The middle tables hold one version per id
        Given the OSM data
            """
            n1 v1 dV t2024-01-01T00:00:00Z x1.0 y1.0
            n1 v2 dV t2024-02-01T00:00:00Z x1.5 y1.0
            n2 v1 dV t2024-01-01T00:00:00Z x2.0 y2.0
            n3 v1 dV t2024-01-01T00:00:00Z x3.0 y3.0
            n3 v2 dD t2024-02-01T00:00:00Z x3.0 y3.0
            w20 v1 dV t2024-01-01T00:00:00Z Tname=road Nn1,n2
            w20 v2 dV t2024-02-01T00:00:00Z Tname=street Nn1,n2
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And table planet_osm_nodes contains exactly
            | id | version |
            | 1  | 2       |
            | 2  | 1       |
        And table planet_osm_ways contains exactly
            | id | version |
            | 20 | 2       |

    Scenario: The log says history is filled from input with several versions
        Given the OSM data
            """
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And the error output contains
            """
            Input holds several versions of objects: filling history from it.
            """

    Scenario: The log says history starts empty from input with one version per object
        Given the OSM data
            """
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            n11 v1 dV t2024-01-01T00:00:00Z Tname=one x11.0 y11.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        And the error output contains
            """
            Input holds one version per object: history starts empty.
            """

    Scenario: Several versions of an object without --history are rejected
        Given the OSM data
            """
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x |
        Then execution fails
        And the error output contains
            """
            Input data is not ordered: node id 10 appears more than once.
            """

    Scenario: Several versions of an object in append mode are rejected
        Given the OSM data
            """
            n10 v1 dV t2024-01-01T00:00:00Z Tname=one x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful

        Given the OSM data
            """
            n10 v2 dV t2024-02-01T00:00:00Z Tname=two x10.0 y10.0
            n10 v3 dV t2024-03-01T00:00:00Z Tname=three x10.0 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution fails
        And the error output contains
            """
            Input data is not ordered: node id 10 appears more than once.
            """

    Scenario: Versions of an object out of order are rejected
        Given the input file 'history-versions-out-of-order.opl'
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution fails
        And the error output contains
            """
            Input data is not ordered: node id 10 version 1 after version 2.
            """

    Scenario: A change file is rejected on create with --history
        Given the input file 'history-change.osc'
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution fails
        And the error output contains
            """
            Reading an OSM change file only works in append mode.
            """
