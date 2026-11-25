Feature: A node change without its way is reported

    The change generator always sends a new way version with the nodes it
    moves. A way rebuilt only because a node moved keeps its own timestamp,
    so its previous version cannot be placed in history and is dropped with
    a warning.

    Scenario: A node moved without its way logs a warning
        Given the lua style
            """
            local lines = osm2pgsql.define_way_table('osm2pgsql_test_lines', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'linestring', projection = 4326 },
            }, { history = true })

            function osm2pgsql.process_way(object)
                lines:insert({ name = object.tags.name, geom = object:as_linestring() })
            end
            """
        And the OSM data
            """
            n1 v1 dV t2024-01-01T00:00:00Z x10.0 y10.0
            n2 v1 dV t2024-01-01T00:00:00Z x10.1 y10.0
            w20 v1 dV t2024-01-01T00:00:00Z Tname=road Nn1,n2
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x |
        Then execution is successful

        Given the OSM data
            """
            n2 v2 dV t2024-02-01T00:00:00Z x10.2 y10.0
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful
        And the error output contains
            """
            1 version(s) of table 'osm2pgsql_test_lines' were replaced by a different version with the same timestamp and are not kept in history.
            """
