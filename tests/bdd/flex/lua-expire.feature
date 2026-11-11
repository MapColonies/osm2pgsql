Feature: Expire configuration in Lua file

    Scenario: Expire on a table must be on geometry column
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some', expire = {{ output = eo }} }
            })
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            Expire only allowed for geometry columns in Web Mercator (3857) or WGS84 (4326) projection.
            """

    @config.have_proj
    Scenario: Expire on a table must be on geometry column in 3857 or 4326
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  projection = 2039,
                  expire = {{ output = eo }} }
            })
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            Expire only allowed for geometry columns in Web Mercator (3857) or WGS84 (4326) projection.
            """

    Scenario: Expire reference must be a userdata ExpireOutput object
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some', type = 'geometry', expire = {{ output = 'abc' }} }
            })
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            Expire output must be of type ExpireOutput.
            """

    Scenario: Expire on a table with 3857 geometry is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {{ output = eo }} }
            })

            function osm2pgsql.process_node(object)
                t:insert({ some = object:as_point() })
            end
            """
        When running osm2pgsql flex
        Then table bar has 1562 rows

    Scenario: Expire on a table with 4326 geometry expires the right tiles
        Given the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                table = 'osm2pgsql_test_expire',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('osm2pgsql_test_points', {
                { column = 'geom',
                  type = 'point',
                  projection = 4326,
                  expire = {{ output = eo }} }
            })

            function osm2pgsql.process_node(object)
                t:insert({ geom = object:as_point() })
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
            | --slim | -a |
        Then execution is successful
        And table osm2pgsql_test_expire contains exactly
            | zoom | x    | y    |
            | 12   | 2161 | 1933 |
            | 12   | 2167 | 1927 |

    Scenario: Directly specifying the expire output is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = eo }
            })

            function osm2pgsql.process_node(object)
                t:insert({ some = object:as_point() })
            end
            """
        When running osm2pgsql flex
        Then table bar has 1562 rows

    Scenario: Expire with buffer option that's not a number fails
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {
                    { output = eo, buffer = 'notvalid' }
                }}
            })
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            Optional expire field 'buffer' must contain a number.
            """

    Scenario: Expire with invalid mode setting fails
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {{ output = eo, mode = 'foo' }} }
            })

            function osm2pgsql.process_node(object)
                t:insert({ some = object:as_point() })
            end
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            Unknown expire mode 'foo'.
            """

    Scenario: Expire with full_area_limit that's not a number fails
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {
                    { output = eo, full_area_limit = true }
                }}
            })
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            Optional expire field 'full_area_limit' must contain a number.
            """

    Scenario: Expire with boundary-only options is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {
                    { output = eo, buffer = 0.2, mode = 'boundary-only' }
                }}
            })

            function osm2pgsql.process_node(object)
                t:insert({ some = object:as_point() })
            end
            """
        When running osm2pgsql flex
        Then table bar has 1562 rows

    Scenario: Expire with hybrid options is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {
                    { output = eo, buffer = 0.2,
                      mode = 'hybrid', full_area_limit = 10000 }
                }}
            })

            function osm2pgsql.process_node(object)
                t:insert({ some = object:as_point() })
            end
            """
        When running osm2pgsql flex
        Then table bar has 1562 rows

    Scenario: Expire with diff_expire that's not a boolean fails
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {
                    { output = eo, diff_expire = 'foo' }
                }}
            })
            """
        When running osm2pgsql flex
        Then execution fails
        And the error output contains
            """
            Optional expire field 'diff_expire' must contain a boolean.
            """

    Scenario: Expire with diff_expire that's a boolean is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                filename = 'bar',
                maxzoom = 12
            })
            osm2pgsql.define_node_table('bar', {
                { column = 'some',
                  type = 'geometry',
                  expire = {
                    { output = eo, diff_expire = true }
                }}
            })
            """
        When running osm2pgsql flex
        Then execution is successful

    Scenario: Expire into table is okay
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                table = 'tiles',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('nodes', {
                { column = 'geom',
                  type = 'point',
                  expire = {
                    { output = eo }
                }}
            })

            function osm2pgsql.process_node(object)
                t:insert({ geom = object:as_point() })
            end
            """
        When running osm2pgsql flex
        Then table nodes has 1562 rows
        And table tiles has 0 rows

    Scenario: Expire into table without maxzoom means maxzoom 0
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                table = 'tiles',
            })
            local t = osm2pgsql.define_node_table('nodes', {
                { column = 'geom',
                  type = 'point',
                  expire = {
                    { output = eo }
                }}
            })

            function osm2pgsql.process_node(object)
                t:insert({ geom = object:as_point() })
            end
            """
        When running osm2pgsql flex
        Then table nodes has 1562 rows
        And table tiles has 0 rows

