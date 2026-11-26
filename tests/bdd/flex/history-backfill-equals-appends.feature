Feature: History filled on create equals history built by appends

    A create with --history from a history file must give the same live
    and history tables as a create from the first state followed by one
    append per later state.

    Background:
        Given the lua style
            """
            local points = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'point', projection = 4326 },
            }, { history = true })

            local polygons = osm2pgsql.define_way_table('osm2pgsql_test_polygons', {
                { column = 'name', type = 'text' },
                { column = 'geom', type = 'polygon', projection = 4326 },
            }, { history = true })

            function osm2pgsql.process_node(object)
                if object.tags.name then
                    points:insert({ name = object.tags.name, geom = object:as_point() })
                end
            end

            function osm2pgsql.process_way(object)
                if object.tags.building then
                    polygons:insert({ name = object.tags.name, geom = object:as_polygon() })
                end
            end
            """
        And the SQL statement keep_backfill
            """
            CREATE TABLE backfill_points AS SELECT * FROM osm2pgsql_test_points;
            CREATE TABLE backfill_points_history AS SELECT * FROM osm2pgsql_test_points_history;
            CREATE TABLE backfill_polygons AS SELECT * FROM osm2pgsql_test_polygons;
            CREATE TABLE backfill_polygons_history AS SELECT * FROM osm2pgsql_test_polygons_history
            """
        And the SQL statement differences
            """
            SELECT
              (SELECT count(*) FROM (
                 (SELECT node_id, name, ST_AsText(geom), valid_from FROM backfill_points
                  EXCEPT ALL
                  SELECT node_id, name, ST_AsText(geom), valid_from FROM osm2pgsql_test_points)
                 UNION ALL
                 (SELECT node_id, name, ST_AsText(geom), valid_from FROM osm2pgsql_test_points
                  EXCEPT ALL
                  SELECT node_id, name, ST_AsText(geom), valid_from FROM backfill_points)) d) AS points,
              (SELECT count(*) FROM (
                 (SELECT node_id, name, ST_AsText(geom), valid_from, valid_to FROM backfill_points_history
                  EXCEPT ALL
                  SELECT node_id, name, ST_AsText(geom), valid_from, valid_to FROM osm2pgsql_test_points_history)
                 UNION ALL
                 (SELECT node_id, name, ST_AsText(geom), valid_from, valid_to FROM osm2pgsql_test_points_history
                  EXCEPT ALL
                  SELECT node_id, name, ST_AsText(geom), valid_from, valid_to FROM backfill_points_history)) d) AS points_history,
              (SELECT count(*) FROM (
                 (SELECT way_id, name, ST_AsText(geom), valid_from FROM backfill_polygons
                  EXCEPT ALL
                  SELECT way_id, name, ST_AsText(geom), valid_from FROM osm2pgsql_test_polygons)
                 UNION ALL
                 (SELECT way_id, name, ST_AsText(geom), valid_from FROM osm2pgsql_test_polygons
                  EXCEPT ALL
                  SELECT way_id, name, ST_AsText(geom), valid_from FROM backfill_polygons)) d) AS polygons,
              (SELECT count(*) FROM (
                 (SELECT way_id, name, ST_AsText(geom), valid_from, valid_to FROM backfill_polygons_history
                  EXCEPT ALL
                  SELECT way_id, name, ST_AsText(geom), valid_from, valid_to FROM osm2pgsql_test_polygons_history)
                 UNION ALL
                 (SELECT way_id, name, ST_AsText(geom), valid_from, valid_to FROM osm2pgsql_test_polygons_history
                  EXCEPT ALL
                  SELECT way_id, name, ST_AsText(geom), valid_from, valid_to FROM backfill_polygons_history)) d) AS polygons_history
            """
        And the SQL statement row_counts
            """
            SELECT (SELECT count(*) FROM backfill_points) AS points,
                   (SELECT count(*) FROM backfill_points_history) AS points_history,
                   (SELECT count(*) FROM backfill_polygons) AS polygons,
                   (SELECT count(*) FROM backfill_polygons_history) AS polygons_history
            """

    Scenario: A backfilled database equals one built by appends
        # Every version of every object, as a history file holds them.
        # n10 v2 and w20 v4 change nothing the Lua writes. n3 moves with w20 v2, then
        # is reused at a new position by w21 when w20 v3 drops it. n10 and
        # w21 are deleted at the end.
        Given the OSM data
            """
            n1 v1 dV t2024-01-01T00:00:00Z x0 y0
            n2 v1 dV t2024-01-01T00:00:00Z x1 y0
            n3 v1 dV t2024-01-01T00:00:00Z x1 y1
            n3 v2 dV t2024-02-01T00:00:00Z x2 y2
            n3 v3 dV t2024-03-01T00:00:00Z x5 y5
            n4 v1 dV t2024-01-01T00:00:00Z x0 y1
            n5 v1 dV t2024-03-01T00:00:00Z x6 y5
            n6 v1 dV t2024-03-01T00:00:00Z x6 y6
            n10 v1 dV t2024-01-01T00:00:00Z Tname=p1 x9 y9
            n10 v2 dV t2024-02-01T00:00:00Z Tname=p1,note=x x9 y9
            n10 v3 dV t2024-03-01T00:00:00Z Tname=p2 x9 y9
            n10 v4 dD t2024-04-01T00:00:00Z x9 y9
            n11 v1 dV t2024-01-01T00:00:00Z Tname=q1 x8 y8
            n11 v2 dV t2024-03-01T00:00:00Z Tname=q1 x8 y7
            w20 v1 dV t2024-01-01T00:00:00Z Tbuilding=yes,name=a Nn1,n2,n3,n4,n1
            w20 v2 dV t2024-02-01T00:00:00Z Tbuilding=yes,name=a Nn1,n2,n3,n4,n1
            w20 v3 dV t2024-03-01T00:00:00Z Tbuilding=yes,name=b Nn1,n2,n4,n1
            w20 v4 dV t2024-04-01T00:00:00Z Tbuilding=yes,name=b,note=x Nn1,n2,n4,n1
            w21 v1 dV t2024-03-01T00:00:00Z Tbuilding=yes,name=c Nn3,n5,n6,n3
            w21 v2 dD t2024-04-01T00:00:00Z
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history |
        Then execution is successful
        When executing the SQL statement keep_backfill

        # The same versions, one state at a time.
        Given the OSM data
            """
            n1 v1 dV t2024-01-01T00:00:00Z x0 y0
            n2 v1 dV t2024-01-01T00:00:00Z x1 y0
            n3 v1 dV t2024-01-01T00:00:00Z x1 y1
            n4 v1 dV t2024-01-01T00:00:00Z x0 y1
            n10 v1 dV t2024-01-01T00:00:00Z Tname=p1 x9 y9
            n11 v1 dV t2024-01-01T00:00:00Z Tname=q1 x8 y8
            w20 v1 dV t2024-01-01T00:00:00Z Tbuilding=yes,name=a Nn1,n2,n3,n4,n1
            """
        When running osm2pgsql flex with parameters
            | --slim | -c | -x | --history | -S | inline_style.lua |
        Then execution is successful

        Given the OSM data
            """
            n3 v2 dV t2024-02-01T00:00:00Z x2 y2
            n10 v2 dV t2024-02-01T00:00:00Z Tname=p1,note=x x9 y9
            w20 v2 dV t2024-02-01T00:00:00Z Tbuilding=yes,name=a Nn1,n2,n3,n4,n1
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful

        Given the OSM data
            """
            n3 v3 dV t2024-03-01T00:00:00Z x5 y5
            n5 v1 dV t2024-03-01T00:00:00Z x6 y5
            n6 v1 dV t2024-03-01T00:00:00Z x6 y6
            n10 v3 dV t2024-03-01T00:00:00Z Tname=p2 x9 y9
            n11 v2 dV t2024-03-01T00:00:00Z Tname=q1 x8 y7
            w20 v3 dV t2024-03-01T00:00:00Z Tbuilding=yes,name=b Nn1,n2,n4,n1
            w21 v1 dV t2024-03-01T00:00:00Z Tbuilding=yes,name=c Nn3,n5,n6,n3
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful

        Given the OSM data
            """
            n10 v4 dD t2024-04-01T00:00:00Z x9 y9
            w20 v4 dV t2024-04-01T00:00:00Z Tbuilding=yes,name=b,note=x Nn1,n2,n4,n1
            w21 v2 dD t2024-04-01T00:00:00Z
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then execution is successful

        Then statement row_counts returns exactly
            | points | points_history | polygons | polygons_history |
            | 1      | 3              | 1        | 3                |
        And statement differences returns exactly
            | points | points_history | polygons | polygons_history |
            | 0      | 0              | 0        | 0                |
