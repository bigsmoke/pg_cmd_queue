-- Complain if script is sourced in `psql`, rather than via `CREATE EXTENSION`.
\echo Use "CREATE EXTENSION pg_cmd_queue" to load this file. \quit

--------------------------------------------------------------------------------------------------------------

comment on extension pg_cmd_queue is
$markdown$
# The `pg_cmd_queue` PostgreSQL extension

The `pg_cmd_queue` PostgreSQL extension offers a framework for creating your
own \*nix or SQL command queues.  Each queue is represented by a table or view
that matches the signature of either:

1. [`nix_queue_cmd_template`](#table-nix_queue_cmd_template), or
2. [`sql_queue_cmd_template`](#table-sql_queue_cmd_template).

Each queue must be registered with the [`cmd_queue`](#table-cmd_queue) table.
That tells the `pg_cmd_queue_runner` daemon where to look for each queue and
how to run each queue.

`pg_cmd_queue` does _not_ come with any preconfigured queues.

## Design philosophy and architecture

`pg_cmd_queue` tries very hard to _not_ impose any additional semantics on top
of those already provided by SQL on the one hand, and the POSIX command-line
interface, HTTP, or indeed SQL again on the other end.

The architecture is a bit deviant, but not overly complicated:

1.  The daemon (`pg_cmdqd`) spawns a command queue runner thread with its own
    Postgres connection for every queue registered in the `cmd_queue` table, for
    which `cmd_queue.queue_is_enabled`.
2.  Command queue runners `SELECT … FOR UPDATE` a single record from the
    relationship identified by the `cmd_queue.cmd_class` primary key column.
    *  By default, the oldest record (according to `rel.cmd_queued_since`) is
       `SELECT`ed each time.
    *  Once `SELECT … FOR UPDATE` no longer yields a row, the (re)select round
       is considered complete and the reselect counter is incremented.
    *  If `cmd_queue.queue_reselect_randomized_every_nth is not null` and the
       reselect counter is divisable by this value, than the order of processing
       is randomized for that reselect round.
3.  The command described in the tuple returned by the `SELECT` statement is
    run.  _How_ the command is run, depends on the `cmd_signature_class`:
    a.  If the _command queue_ table or view is derived from the
        `nix_queue_cmd_template`, then the \*nix command described by the
        `cmd_argv`, `cmd_env` and `cmd_stdin` is executed using the POSIX
        interface.  The `cmd_stdout`, `cmd_stderr` and `cmd_exit_code` or
        `cmd_term_sig` of the command are captured.
    b.  If the _command queue_ relationship is derived from the
        `sql_queue_cmd_template`, then the command specification is much
        simpler; it's just a `sql_cmd`.  The results of running this `sql_cmd`
        are collected into `cmd_sql_result_status`, `cmd_sql_fatal_error`,
        `cmd_sql_nonfatal_errors` and `cmd_sql_result_rows`.
4.  Still within the same transaction, an `UPDATE` is performed on the record
    (whether from a table or a view) identified by what should be a unique
    `cmd_id`/`cmd_subid` combination.
    *  `cmd_subid` may be `NULL` as it is matched using `IS NOT DISTINCT FROM`.
    *  The `UPDATE` always communicates back the `cmd_runtime` to the queue.
    *  In addition, in the case of a `nix_queue_cmd`, the `UPDATE` sets the
       `cmd_exit_code`, `cmd_term_sig`, `cmd_stdout` and `cmd_stderr`.
    *  Whereas, in the case of a `sql_queue_cmd`, the `UPDATE` sets
       `cmd_sql_result_status`, `cmd_sql_fatal_error`,
       `cmd_sql_nonfatal_errors` and `cmd_sql_result_rows`.

It is the responsibility of the developer using `pg_cmd_queue` to:

1.  make sure that commands appear and disappear from the queue when necessary;
2.  log errors outside of the queue;
3.  etc.

This is all considered application-specific logic, about which `pg_cmd_queue`
has no opinion, though it does offer some helpful, readymade trigger functions
and the likes.

## Installing `pg_cmd_queue`

The `pg_cmd_queue` `Makefile` uses PostgreSQL's build infrastructure for
extensions: [PGXS](https://www.postgresql.org/docs/current/extend-pgxs.html).
This is not always installed by default.  For PostgreSQL 15 on Ubuntu 15, for
example, this requires installing the `postgresql-server-dev-15` package.

```
make
```

Subsequently, you can install the extension files and the queue runner binary:

```
make install
```

This requires write access to `pg_config --sharedir`.  On most systems this
means that you will have to become `root` first or run `sudo make install`.

## Setting up `pg_cmd_queue`

First, you need a PostgreSQL role with which the main thread of the
`pg_cmd_queue_runner` daemon can connect to the database.  This role needs to
be granted:

  * `USAGE` permission on the `cmdq` schema;
  * `SELECT` permission on the `cmd_queue` table;
  * membership of each `queue_runner_role` in the `cmd_queue` table.

Second, you need to create a relation to represent your queue.  This requires
you to choose between ⓐ a table to hold your queue, or ⓑ a view.  For a queue
which does not immediately need to support a high throughput, a view will
often be the simplest.  See the [`test__pg_cmd_queue()`
procedure](#procedure-test__pg_cmd_queue) for a full example.

When creating your own `_cmd` tables or views, you may add additional
columns of your own, _after_ the columns that are specified by the
`_<cmd_type>_queue_cmd_template` that you choose to use.  Note that your custom
column names are _not_ allowed to start with `queue_` or `cmd_`.

### View-based command queues

The nice thing about a view-based queue is that it will always be up-to-date,
as long as the predicates the view definition are correct.

### Table-based command queues

To create a table-based queue is simple:

```sql
create table cmdq.my_cmd (
    like cmdq.nix_queue_cmd_template including all
);
```

Or:

```sql
create table cmdq.my_cmd (
    like cmdq.sql_queue_cmd_template including all
);
```

To fill it automatically, however, you will need some triggers elsewhere.

To not keep commands stuck in the queue table forever, at some point, you are
going to have to delete them.  For that, there's a readymade trigger function.
Hook it on like this:

```sql
create trigger delete_after_update
    after update
    on cmdq.my_cmd
    for each row
    execute cmdq.queue_cmd__delete_after_update();
```

However, without an accompanying `BEFORE` trigger that has a bit of error
checking, commands would be deleted from the queue indiscriminately, regardless
of whether they succeeded or failed.  This fits wholly within the design
philosopy of `pg_cmd_queue`, because whether you actually consider a
`cmd_exit_code = 12` a failure is up to you and your application logic, and
the same goes for `cmd_sql_result_status = 'PGRES_FATAL_ERROR'`.  It's all up
to you.  A `nix_queue_cmd__require_exit_success()` trigger function _is_
provided for you for free, as is a `sql_queue_cmd__require_status_ok()`
function.

### Security considerations

Be mindful of the fact that anybody with the ability to register queues in the
[`cmd_queue`](#table-cmd_queue) table or who can make indidivual commands appear
in one of the registered queues, will have their \*nix privileges escalated to:

* _at least_ the level of the user and group the the `pg_cmdqd` runs at, in the
  case of a queue matching the `nix_queue_cmd_template` signature; and/or
* the level of privileges granted to the `queue_runner_role`, both in the case
  of `sql_queue_cmd_template`-type command queues and during `UPDATE`s performed
  on any command queue relationship.

## Running `pg_cmdqd` / `pg_command_queue_daemon`

*nix commands executed by `pg_cmdqd`, are passed the following environment
variables:

  * the `PATH` with which `pg_cmdqd` was executed.

## `pg_cmd_queue` settings

| Setting name                          | Default setting  |
| ------------------------------------- | ---------------- |
| `pg_cmd_queue.notify_channel`         | `cmdq`           |

## Planned features for `pg_cmd_queue`

* Helpers for setting up partitioning for table-based queues, to easily get rid
  of table bloat.
* Allow per-queue configuration of effective user and group to run *nix commands
  as?  Or should we “just” shelve off this functionality to `sudo` and the
  likes?
* `http_queue_cmd_template` support using `libcurl` would be f-ing awesome.
* `pg_cron`'s `cron` schema compatibility, so that you can use `pg_cron` without
  using `pg_cron`. 😉
* pgAgent schema compatibility, so that you can set up pgAgent jobs (in pgAdmin,
  for example) without having to run pgAgent.

## Features that will _not_ be part of `pg_cmd_queue`

* There will be no logging in `pg_cmd_queue`.  How to handle successes and
  failures is up to triggers on the `cmd_class` tables or views which
  will be updated by the `pg_cmd_queue_runner` daemon after running a command.

* There will be no support for SQL callbacks, not even in `sql_queue_cmd`
  queues.  Again, this would be up to the implementor of a specific queue.
  If you want to use the generic `sql_queue_cmd` queue, just make sure that
  error handling logic is included in the `sql_cmd`.

## Related/similar PostgreSQL programs and extensions

* [`pgAgent`](https://www.pgadmin.org/docs/pgadmin4/latest/pgagent.html) is the
   OG of Postgres task schedulers.  To add jobs from SQL is quite cumbersome,
   though.  It really seems to be primarily designed for adding jobs via
   pgAdmin, which the author of `pg_cmd_queue` personally doesn't dig much.
   (He _much_ prefers `psql`.)

* [`pg_cron`](https://github.com/citusdata/pg_cron) is a simpler, more Unixy
  approach to the execution of periodic jobs than `pgAgent`.  In particular,
  `pg_cron` has a friendlier SQL API for creating and managing cron jobs.

* [`pgsidekick`](https://github.com/wttw/pgsidekick) is a collection of small
  programs that _don't_ require the installation of a Postgres extensions.
  Each of the little programs work by `LISTEN`ing to a specific `NOTIFY` channel
  and doing something when a notification event is caught.

* [`pqasyncnotifier`](https://github.com/twosigma/postgresql-contrib/blob/master/pqasyncnotifier.c)
  is a single, simple `libpq` program that `LISTEN`s for `NOTIFY` events and
  outputs them in a form that makes it easy to pipe them to `xargs` and do
  something fun with a shell command.

## Extension object reference

<?pg-readme-reference context-division-depth="2" context-division-is-self="true" ?>


## Extension authors and contributors

* [Rowan](https://www.bigsmoke.us/) originated this extension in 2023 while
  developing the PostgreSQL backend for the [FlashMQ SaaS MQTT cloud
  broker](https://www.flashmq.com/).  Rowan does not like to see himself as a
  tech person or a tech writer, but, much to his chagrin, [he
  _is_](https://blog.bigsmoke.us/category/technology). Some of his chagrin
  about his disdain for the IT industry he poured into a book: [_Why
  Programming Still Sucks_](https://www.whyprogrammingstillsucks.com/).  Much
  more than a “tech bro”, he identifies as a garden gnome, fairy and ork rolled
  into one, and his passion is really to [regreen and reenchant his
  environment](https://sapienshabitat.com/).  One of his proudest achievements
  is to be the third generation ecological gardener to grow the wild garden
  around his beautiful [family holiday home in the forest of Norg, Drenthe,
  the Netherlands](https://www.schuilplaats-norg.nl/) (available for rent!).

## Colophon

<?pg-readme-colophon context-division-depth="2" context-division-is-self="true" ?>
$markdown$;

--------------------------------------------------------------------------------------------------------------

create function pg_cmd_queue_readme()
    returns text
    volatile
    set search_path from current
    set pg_readme.include_view_definitions to 'true'
    set pg_readme.include_routine_definitions_like to '{test__%}'
    language plpgsql
    as $plpgsql$
declare
    _readme text;
begin
    create extension if not exists pg_readme;

    _readme := pg_extension_readme('pg_cmd_queue'::name);

    raise transaction_rollback;  -- to `DROP EXTENSION` if we happened to `CREATE EXTENSION` for just this.
exception
    when transaction_rollback then
        return _readme;
end;
$plpgsql$;

comment on function pg_cmd_queue_readme() is
$md$This function utilizes the `pg_readme` extension to generate a thorough README for this extension, based on the `pg_catalog` and the `COMMENT` objects found therein.
$md$;

--------------------------------------------------------------------------------------------------------------

create function pg_cmd_queue_meta_pgxn()
    returns jsonb
    stable
    set search_path from current
    language sql
    return jsonb_build_object(
        'name'
        ,'pg_cmd_queue'
        ,'abstract'
        ,'Postgres-native Unix/SQL command queue framework'
        ,'description'
        ,'The pg_cmd_queue PostgreSQL extension provides a framework for handling queues of Unix or SQL'
            ' commands, with a ready-to-go runner daemon.'
        ,'version'
        ,(
            select
                pg_extension.extversion
            from
                pg_catalog.pg_extension
            where
                pg_extension.extname = 'pg_cmd_queue'
        )
        ,'maintainer'
        ,array[
            'Rowan Rodrik van der Molen <rowan@bigsmoke.us>'
        ]
        ,'license'
        ,'postgresql'
        ,'prereqs'
        ,'{
            "development": {
                "recommends": {
                    "pg_readme": 0
                }
            },
            "runtime": {
                "requires": {
                    "hstore": 0
                }
            },
            "test": {
                "requires": {
                    "pgtap": 0
                }
            }
        }'::jsonb
        ,'provides'
        ,('{
            "pg_cmd_queue": {
                "file": "pg_cmd_queue--0.1.0.sql",
                "version": "' || (
                    select
                        pg_extension.extversion
                    from
                        pg_catalog.pg_extension
                    where
                        pg_extension.extname = 'pg_cmd_queue'
                ) || '",
                "docfile": "README.md"
            }
        }')::jsonb
        ,'resources'
        ,'{
            "homepage": "https://blog.bigsmoke.us/tag/pg_cmd_queue",
            "bugtracker": {
                "web": "https://github.com/bigsmoke/pg_cmd_queue/issues"
            },
            "repository": {
                "url": "https://github.com/bigsmoke/pg_cmd_queue.git",
                "web": "https://github.com/bigsmoke/pg_cmd_queue",
                "type": "git"
            }
        }'::jsonb
        ,'meta-spec'
        ,'{
            "version": "1.0.0",
            "url": "https://pgxn.org/spec/"
        }'::jsonb
        ,'generated_by'
        ,'`select pg_cmd_queue_meta_pgxn()`'
        ,'tags'
        ,array[
            'queue'
            ,'plpgsql'
            ,'view'
            ,'table'
        ]
    );

comment on function pg_cmd_queue_meta_pgxn() is
$md$Returns the JSON meta data that has to go into the `META.json` file needed for PGXN—PostgreSQL Extension Network—packages.

The `Makefile` includes a recipe to allow the developer to: `make META.json` to
refresh the meta file with the function's current output, including the
`default_version`.

`pg_cmd_queue` can be found on PGXN: https://pgxn.org/dist/pg_readme/
$md$;

--------------------------------------------------------------------------------------------------------------

create function pg_cmd_queue_notify_channel()
    returns text
    stable
    leakproof
    parallel safe
    language sql
    return coalesce(nullif(current_setting('pg_cmd_queue.notify_channel', true), ''), 'cmdq');

--------------------------------------------------------------------------------------------------------------

create function pg_cmd_queue_search_path()
    returns text
    stable
    leakproof
    parallel safe
    language sql
    return (
        with recursive recursive_requirement as (
            select
                a.name as extension_name
                ,v.requires as required_extensions
                ,v.schema as schema_name
                ,0 as dependency_depth
            from
                pg_catalog.pg_available_extensions as a
            inner join
                pg_catalog.pg_available_extension_versions as v
                on v.name = a.name
                and v.version = a.default_version
            where
                a.name = 'pg_cmd_queue'
            union all
            select
                e.extname as extension_name
                ,v.requires as required_extensions
                ,e.extnamespace::regnamespace::name as schema_name
                ,r.dependency_depth + 1
            from
                recursive_requirement as r
            cross join lateral
                unnest(r.required_extensions) as required(extension_name)
            inner join
                pg_catalog.pg_extension as e
                on e.extname = required.extension_name
            inner join
                pg_catalog.pg_available_extension_versions as v
                on v.name = e.extname
                and v.version = e.extversion
            where
                r.required_extensions is not null
        )
        select
            string_agg(r.schema_name, ', ' order by r.dependency_depth)
                || ', pg_catalog, pg_temp'  -- To make the implicit explit.
        from
            recursive_requirement as r
    );

comment on function pg_cmd_queue_search_path() is
$md$Determing the `search_path` for within extension scripts similar to how `CREATE EXTENSION` would set it.

This allows us to easily set the `search_path` correctly when we are outside
the context of `CREATE EXTENSION`, for example:

1. when debugging a `.sql` script using `bin/debug-extension.sh`, or
2. while running testcases.
$md$;

-- Allow us to find the `cmdq` schema as well as the schema in which our dependencies (i.e., `hstore`) are
-- installed in.
\echo if_debug select set_config('search_path', pg_cmd_queue_search_path(), true);

--------------------------------------------------------------------------------------------------------------

create table cmd_queue (
    cmd_class regclass
        primary key
        check (
            (parse_ident(cmd_class::text))[
                array_upper(parse_ident(cmd_class::text), 1)
            ] ~ '^[a-z][a-z0-9_]+_cmd$'
        )
    ,cmd_signature_class regclass
        not null
        check (
            (parse_ident(cmd_signature_class::text))[
                array_upper(parse_ident(cmd_signature_class::text), 1)
            ] in ('nix_queue_cmd_template', 'sql_queue_cmd_template')
        )
    ,queue_runner_role name
    ,queue_notify_channel name
    ,queue_reselect_interval interval
        not null
        default '5 minutes'::interval
    ,queue_reselect_randomized_every_nth int
        check (queue_reselect_randomized_every_nth is null or queue_reselect_randomized_every_nth > 0)
    ,queue_select_timeout interval
        default '10 seconds'::interval
    ,queue_cmd_timeout interval
    /*
    ,queue_runner_range int4range
        not null
        default int4range(1, 2)
    */
    ,queue_is_enabled bool
        not null
        default true
    ,queue_wait_time_limit_warn interval
    ,queue_wait_time_limit_crit interval
    ,queue_registered_at timestamptz
        not null
        default now()
    ,queue_metadata_updated_at timestamptz
        not null
        default now()
    ,pg_extension_name text
    ,constraint warn_limit_must_be_greater_than_reselect_interval
        check ((queue_wait_time_limit_warn > queue_reselect_interval) is not false)
    ,constraint crit_limit_must_be_greater_than_warn_limit
        check ((queue_wait_time_limit_crit > queue_wait_time_limit_warn) is not false)
    ,constraint crit_limit_must_be_greater_than_reselect_interval
        check ((queue_wait_time_limit_crit > queue_reselect_interval) is not false)
);

comment on table cmd_queue is
$md$Every table or view which holds individual queue commands has to be registered as a record in this table.
$md$;

comment on column cmd_queue.queue_runner_role is
$md$This is the role as which the queue runner should select from the queue and run update commands.
$md$;

select pg_catalog.pg_extension_config_dump('cmd_queue', 'WHERE pg_extension_name IS NULL');

--------------------------------------------------------------------------------------------------------------

create function cmd_queue__update_updated_at()
    returns trigger
    set search_path from current
    language plpgsql
    as $$
begin
    assert tg_when = 'BEFORE';
    assert tg_op = 'UPDATE';
    assert tg_level = 'ROW';
    assert tg_table_schema = 'cmdq';
    assert tg_table_name = 'cmd_queue';

    NEW.queue_metadata_updated_at = now();

    return NEW;
end;
$$;

create trigger update_updated_at
    before update
    on cmd_queue
    for each row
    execute function cmd_queue__update_updated_at();

--------------------------------------------------------------------------------------------------------------

create function cmd_queue__create_queue_signature_downcast()
    returns trigger
    set search_path from current
    language plpgsql
    as $$
declare
    _extension_context_detection_object name;
    _extension_context name;
begin
    assert tg_when = 'AFTER';
    assert tg_op in ('INSERT', 'UPDATE', 'DELETE');
    assert tg_level = 'ROW';
    assert tg_table_schema = 'cmdq';
    assert tg_table_name = 'cmd_queue';

    -- The extension context may be:
    --    a) outside of a `CREATE EXTENSION` / `ALTER EXTENSION` context (`_extension_context IS NULL`);
    --    b) inside the `CREATE EXTENSION` / `ALTER EXTENSION` context of the extension owning the config
    --       table to which this trigger is attached; or
    --    c) inside the `CREATE EXTENSION` / `ALTER EXTENSION` context of extension that changes settings in
    --       another extension's configuration table.
    _extension_context_detection_object := format(
        'extension_context_detector_%s'
        ,floor(pg_catalog.random() * 1000)
    );
    execute format('CREATE TEMPORARY TABLE %I (col int) ON COMMIT DROP', _extension_context_detection_object);
    select
        pg_extension.extname
    into
        _extension_context
    from
        pg_catalog.pg_depend
    inner join
        pg_catalog.pg_extension
        on pg_extension.oid = pg_depend.refobjid
    where
        pg_depend.classid = 'pg_catalog.pg_class'::regclass
        and pg_depend.objid = _extension_context_detection_object::regclass
        and pg_depend.refclassid = 'pg_catalog.pg_extension'::regclass
    ;
    execute format('DROP TABLE %I', _extension_context_detection_object);

    if tg_op = 'DELETE' then
        execute format(
            'DROP CAST (%2$s AS %1$s)'
            ,OLD.cmd_signature_class::text
            ,OLD.cmd_class::text
        );
        execute format(
            'DROP FUNCTION %1$s(%2$s)'
            ,OLD.cmd_signature_class::text
            ,OLD.cmd_class::text
        );
    end if;

    if tg_op in ('INSERT', 'UPDATE') then
        if  tg_op = 'UPDATE'
            and OLD.pg_extension_name is not null
            and _extension_context is distinct from OLD.pg_extension_name
        then
            execute format(
                'ALTER EXTENSION %3$I DROP FUNCTION %1$s(%2$s)'
                ,NEW.cmd_signature_class::text
                ,NEW.cmd_class::text
                ,NEW.pg_extension_name
            );
            if _extension_context is not null then
                execute format(
                    'ALTER EXTENSION %3$I ADD FUNCTION %1$s(%2$s)'
                    ,NEW.cmd_signature_class::text
                    ,NEW.cmd_class::text
                    ,_extension_context
                );
            end if;
        end if;

        execute format(
            'CREATE OR REPLACE FUNCTION %1$s(%2$s)'
            ' RETURNS %1$s'
            ' SET search_path FROM CURRENT'
            ' IMMUTABLE LEAKPROOF PARALLEL SAFE LANGUAGE SQL'
            ' AS $sql$SELECT null::%1$s #= hstore($1);$sql$'
            ,NEW.cmd_signature_class::text
            ,NEW.cmd_class::text
        );

        if _extension_context is not null and _extension_context is distinct from NEW.pg_extension_name then
            execute format(
                'ALTER EXTENSION %3$I DROP FUNCTION %1$s(%2$s)'
                ,NEW.cmd_signature_class::text
                ,NEW.cmd_class::text
                ,_extension_context
            );
        end if;
        if  NEW.pg_extension_name is not null
            and NEW.pg_extension_name is distinct from _extension_context
        then
            execute format(
                'ALTER EXTENSION %3$I ADD FUNCTION %1$s(%2$s)'
                ,NEW.cmd_signature_class::text
                ,NEW.cmd_class::text
                ,NEW.pg_extension_name
            );
        end if;
    end if;

    if tg_op = 'INSERT' then
        execute format(
            'CREATE CAST (%2$s AS %1$s) WITH FUNCTION %1$s(%2$s) AS IMPLICIT'
            ,NEW.cmd_signature_class::text
            ,NEW.cmd_class::text
        );
        if _extension_context is not null and _extension_context is distinct from NEW.pg_extension_name then
            execute format(
                'ALTER EXTENSION %3$I DROP CAST (%2$s AS %1$s)'
                ,NEW.cmd_signature_class::text
                ,NEW.cmd_class::text
                ,_extension_context
            );
        end if;
        if  NEW.pg_extension_name is not null
            and NEW.pg_extension_name is distinct from _extension_context
        then
            execute format(
                'ALTER EXTENSION %3$I ADD CAST (%2$s AS %1$s)'
                ,NEW.cmd_signature_class::text
                ,NEW.cmd_class::text
                ,NEW.pg_extension_name
            );
        end if;
    end if;

    return null;
end;
$$;

create trigger create_queue_signature_downcast
    after insert or update or delete
    on cmd_queue
    for each row
    execute function cmd_queue__create_queue_signature_downcast();

--------------------------------------------------------------------------------------------------------------

create function cmd_queue__queue_signature_constraint()
    returns trigger
    set search_path from current
    language plpgsql
    as $$
declare
    _queue_rel_attrs text[];
    _extra_attr name;
    _signature_attrs text[];
    _signature_attr_count int;
begin
    assert tg_when = 'AFTER';
    assert tg_op = 'INSERT';
    assert tg_level = 'ROW';
    assert tg_table_schema = 'cmdq';
    assert tg_table_name = 'cmd_queue';

    select
        array_agg(attname::text || ' ' || atttypid::regtype::text order by attnum)
    into
        _signature_attrs
    from
        pg_catalog.pg_attribute
    where
        attrelid = NEW.cmd_signature_class
        and attnum > 1
    ;
    _signature_attr_count := array_length(_signature_attrs, 1);

    select
        array_agg(attname::text || ' ' || atttypid::regtype::text order by attnum)
    into
        _queue_rel_attrs
    from
        pg_catalog.pg_attribute
    where
        attrelid = NEW.cmd_class
        and attnum > 1
    ;

    if _queue_rel_attrs[1:_signature_attr_count] != _signature_attrs then
        raise integrity_constraint_violation using
            message = format(
                'The first %s columns of the queue table `%s` do not match the `%s` signature table.'
                ,_signature_attr_count
                ,NEW.cmd_class
                ,NEW.cmd_signature_class
            )
            ,detail = format('%s ≠ %s', _queue_rel_attrs[1:_signature_attr_count], _signature_attrs)
            ,hint = format(
                'You can derive your queue table from `%s` or `%s`.'
                ,'nix_queue_cmd_template'::regclass
                ,'sql_queue_cmd_template'::regclass
            );
    end if;

    foreach _extra_attr in array _queue_rel_attrs[(_signature_attr_count+1):] loop
        if _extra_attr like 'queue__' then
            raise integrity_constraint_violation using
                message = '`queue_` is a reserved prefix and not allowed in your own custom/extra columns.'
                ,detail = format('Illegal custom column name: `%I`', _extra_attr);
        end if;
        if _extra_attr like 'cmd__' then
            raise integrity_constraint_violation using
                message = '`cmd_` is a reserved prefix and not allowed in your own custom/extra columns.'
                ,detail = format('Illegal custom column name: `%I`', _extra_attr);
        end if;
    end loop;

    return null;
end;
$$;

create constraint trigger queue_signature_constraint
    after insert
    on cmd_queue
    for each row
    execute function cmd_queue__queue_signature_constraint();

--------------------------------------------------------------------------------------------------------------

create function cmd_queue__notify_daemon_of_changes()
    returns trigger
    set search_path from current
    language plpgsql
    as $$
declare
    _channel text := pg_cmd_queue_notify_channel();
    _cmd_class_identity text := (
        select
            i.identity
        from
            pg_identify_object(
                'pg_class'::regclass
                ,coalesce(NEW.cmd_class, OLD.cmd_class)
                ,0
            ) as i
    );
begin
    assert tg_when = 'AFTER';
    assert tg_op in ('INSERT', 'UPDATE', 'DELETE');
    assert tg_level = 'ROW';
    assert tg_table_schema = 'cmdq';
    assert tg_table_name = 'cmd_queue';

    perform pg_notify(_channel, row(tg_table_name, _cmd_class_identity, tg_op)::text);

    return null;
end;
$$;

create trigger notify_daemon_of_changes
    after insert or update or delete
    on cmd_queue
    for each row
    execute function cmd_queue__notify_daemon_of_changes();

--------------------------------------------------------------------------------------------------------------

create function cmd_class_color(regclass)
    returns table (
        r int
        ,g int
        ,b int
        ,hex text
        ,ansi_fg text
        ,ansi_bg text
    )
    immutable
    leakproof
    parallel safe
begin atomic
    with fqn as (
        select
            relnamespace::regnamespace::text || '.' || relname  as fully_qualified_name
        from
            pg_catalog.pg_class
        where
            oid = $1
    )
    ,hash as (
        select
            md5(fully_qualified_name) as cmd_class_hash
        from
            fqn
    )
    ,color_space as (
        select
            r, g, b
            ,rank() over (order by r, g, b) as color_no
        from
            generate_series(100, 240, 10) as r
        cross join
            generate_series(100, 240, 10) as g
        cross join
            generate_series(100, 240, 10) as b
        where
            not (abs(r-g) < 30 and abs(g-b) < 30)  -- Cut out greys
    )
    ,number as (
        select
            ('x' || substring(md5(cmd_class_hash), 1, 3))::bit(11)::int as hash_no
        from
            hash
    )
    ,distinct_color as (
        select
            r,g,b
        from
            color_space
        where
            color_space.color_no = (select hash_no from number)
    )
    select
        rgb.*
        ,to_hex(rgb.r) || to_hex(rgb.g) || to_hex(rgb.b) as hex
        ,format(E'[38;2;%s;%s;%sm', rgb.r, rgb.g, rgb.b) as ansi_fg
        ,format(E'[48;2;%s;%s;%sm', rgb.r, rgb.g, rgb.b) as ansi_bg
    from
        distinct_color as rgb
    ;
end;

--------------------------------------------------------------------------------------------------------------
-- `queue_cmd_template` table template and related objects                                                  --
--------------------------------------------------------------------------------------------------------------

create table queue_cmd_template (
    cmd_class regclass
        not null
        references cmd_queue (cmd_class)
            on delete cascade
            on update cascade
    ,cmd_id text
        not null
    ,cmd_subid text
    ,cmd_queued_since timestamptz
        not null
        default now()
    ,cmd_runtime tstzrange
    ,unique nulls not distinct (cmd_id, cmd_subid)
);

comment on column queue_cmd_template.cmd_id is
$md$Uniquely identifies an individual command in the queue (unless if `cmd_subid` is also required).

When a single key in the underlying object of a queue command is sufficient to
identify it, a `::text` representation of the key should go into this column.
If multiple keys are needed—for example, when the underlying object has a
multi-column primary key or when each underlying object can simultaneously
appear in multiple commands the queue—you will want to use `cmd_subid` in
addition to `cmd_id`.
$md$;

comment on column queue_cmd_template.cmd_subid is
$md$Helps `cmd_id` to uniquely identify commands in the queue, when just a `cmd_id` is not enough.
$md$;

--------------------------------------------------------------------------------------------------------------

create function queue_cmd_template__no_insert()
    returns trigger
    set search_path to pg_catalog
    language plpgsql
    as $$
begin
    assert tg_when = 'BEFORE';
    assert tg_op = 'INSERT';
    assert tg_level = 'ROW';
    assert tg_table_schema = 'cmdq';
    assert tg_table_name ~ 'queue_cmd_template$';

    raise exception 'Don''t directly INSERT into this template table.';
end;
$$;

--------------------------------------------------------------------------------------------------------------

create trigger no_insert
    before insert
    on queue_cmd_template
    for each row
    execute function queue_cmd_template__no_insert();

--------------------------------------------------------------------------------------------------------------

create function queue_cmd__ignore_update()
    returns trigger
    set search_path to pg_catalog
    language plpgsql
    as $$
begin
    assert tg_when in ('BEFORE', 'INSTEAD OF');
    assert tg_op = 'UPDATE';
    assert tg_level = 'ROW';

    return null;
end;
$$;

--------------------------------------------------------------------------------------------------------------

create function queue_cmd__notify()
    returns trigger
    set search_path from current
    language plpgsql
    as $$
declare
    _queue_notify_channel name := coalesce(
        nullif(current_setting('pg_cmd_queue.notify_channel', true), '')
        ,'cmdq'
    );
    _cmd_class_qualified text := quote_ident(tg_table_schema) || '.' || quote_ident(tg_table_name);
    _cmd_id_expression name := '($1).cmd_id::text';
    _cmd_subid_expression name := '($1).cmd_subid::text';
    _cmd_id text;
    _cmd_subid text;
begin
    assert tg_when = 'AFTER';
    assert tg_op in ('INSERT', 'UPDATE', 'DELETE');
    assert tg_level = 'ROW';
    assert tg_table_schema = 'cmdq' and tg_nargs in (0, 1)
           or tg_table_name != 'cmdq' and tg_nargs between 0 and 4;

    if tg_nargs > 0 then
        _queue_notify_channel := coalesce(nullif(tg_argv[0], 'DEFAULT'), _queue_notify_channel);
    end if;
    if tg_nargs > 1 then
        _cmd_class_qualified := coalesce(nullif(tg_argv[1], 'DEFAULT'), _cmd_class_qualified);
    end if;
    if tg_nargs > 2 then
        _cmd_id_expression := case
            when upper(tg_argv[2]) = 'DEFAULT' then
                _cmd_id_expression
            when tg_argv[2] ~ '^\(.*\)::text$' then
                regexp_replace(tg_argv[2], '(?:OLD|NEW)\.', '($1).')
            else
                '($1).' || quote_ident(tg_argv[2])
        end;
    end if;
    if tg_nargs > 3 then
        _cmd_subid_expression := case
            when upper(tg_argv[3]) = 'DEFAULT' then
                _cmd_subid_expression
            when tg_argv[3] ~ '^\(.*\)::text$' then
                regexp_replace(tg_argv[3], '(?:OLD|NEW)\.', '($1).')
            else
                '($1).' || quote_ident(tg_argv[3])
        end;
    end if;

    if tg_op = 'INSERT' then
        execute 'SELECT ' || _cmd_id_expression using NEW into _cmd_id;
        execute 'SELECT ' || _cmd_subid_expression using NEW into _cmd_subid;
    elsif tg_op in ('UPDATE', 'DELETE') then
        execute 'SELECT ' || _cmd_id_expression using OLD into _cmd_id;
        execute 'SELECT ' || _cmd_subid_expression using OLD into _cmd_subid;
    end if;

    perform pg_notify(_queue_notify_channel, row(_cmd_class_qualified, _cmd_id, _cmd_subid)::text);

    return null;
end;
$$;

comment on function queue_cmd__notify() is
$md$Use this trigger function for easily triggering `NOTIFY` events from a queue's (underlying) table.

When using a table to hold a queue's commands, you can hook this trigger
function directly to an `ON INSERT` trigger on the `queue_cmd_template`-derived
table itself.  In that case, the trigger needs no arguments.  The notifications
will be sent on the channel configured in the extension-global
[`pg_cmd_queue.notify_channel`](#pg_cmd_queue-settings) setting. If that is so
desired, you can keep

The only argument that this trigger function absolutely requires is the
`NOTIFY` channel name (which should be identical to the channel name in the
`cmd_queue.queue_notify_channel` column.  In case that the trigger is attached
to a table or view in the `cmdq` schema with the relation's name ending in
`_cmd`, that is also the only _allowed_ argument.


When the `queue_cmd_template`-derived relation is a view, this trigger function
will have to be attached to the underlying table and will require three
additional arguments: ① the name of the field which will be mapped to `cmd_id`
in the view; and the name of the field which will be mapped to
`cmd_subid` in the view.  If there is no `cmd_subid`, this third parameter
should be `null`.

When the trigger is created on a table or view in the `cmdq` schema, only one
parameter is accepted, because the signature for

| No. | Trigger param           | Default         | Example values                                    |
| --- | ----------------------- | --------------- | ------------------------------------------------- |
|  1. | `queue_notify_channel`  | `TG_TABLE_NAME` | `'my_notify_channel'`                             |
|  2. | `queue_cmd_relname`     | `TG_TABLE_NAME` | `'my_cmd'`                                        |
|  3. | `cmd_id_source`         | `'cmd_id'`      | `'field'`, `'(NEW.field || ''-suffix'')::text'`   |
|  4. | `cmd_subid_source`      | `'cmd_subid'`   | `'field'`, `'NULL'`, `'(''invoice_mail'')::text'` |

1. The first argument (`queue_notify_channel`) defaults to the name of the
   relationship to which the trigger is attached.
2. The second argument (`queue_cmd_relname`) also defaults to the name of the
   relationship to which the trigger is attached.  In case that the trigger is
   created on the _underlying table_ for a _view_ in the `cmdq` schema,
3. The third argument (`cmd_id_source`) defaults to `'cmd_id'`, which is
   probably what you want

$md$;

--------------------------------------------------------------------------------------------------------------

create function queue_cmd__insert_elsewhere()
    returns trigger
    language plpgsql
    as $$
declare
    _cmd_id_field name := 'cmd_id';
    _cmd_subid_field name := 'cmd_subid';
    _other_relid regclass;
begin
    assert tg_when = 'BEFORE';
    assert tg_op = 'UPDATE';
    assert tg_level = 'ROW';
    assert tg_nargs = 1;

    _other_relid := tg_argv[0]::regclass;

    execute format('INSERT INTO %s VALUES (($1).*)', _other_relid) using NEW;

    return NEW;
end;
$$;

--------------------------------------------------------------------------------------------------------------

create function nix_queue_cmd__require_exit_success()
    returns trigger
    set search_path from current
    language plpgsql
    as $$
begin
    assert tg_when in ('BEFORE', 'AFTER');
    assert tg_op = 'UPDATE';
    assert tg_level = 'ROW';

    if NEW.cmd_exit_code > 0 then
        raise exception using
            errcode = 'PE' || right('000' || NEW.cmd_exit_code::text,  3)
            ,message = format(
                E'Cought non-zero exit code %s for `cmd_id = %L` in `%s` queue.'
                ,NEW.cmd_exit_code
                ,OLD.cmd_id
                ,OLD.cmd_class
            )
            ,detail = (
                'cmd_line: ' || cmd_line(NEW.cmd_argv, NEW.cmd_env)
                || coalesce(E'\ncmd_stderr: ' || convert_from(nullif(NEW.cmd_stderr, ''::bytea), 'UTF-8'),  '')
                || coalesce(E'\ncmd_stdout: ' || convert_from(nullif(NEW.cmd_stdout, ''::bytea), 'UTF-8'),  '')
            )
            ,schema = tg_table_schema
            ,table = tg_table_name
            ,column = 'cmd_exit_code'
        ;
    end if;

    if NEW.cmd_term_sig is not null then
        raise exception using
            errcode = 'PS' || right('000' || NEW.cmd_term_sig::text,  3)
            ,message = format(
                E'Cought termination signal %s for `cmd_id = %L` in `%s` queue.'
                ,NEW.cmd_term_sig
                ,OLD.cmd_id
                ,OLD.cmd_class
            )
            ,detail = format(
                'cmd_line: ' || cmd_line(NEW.cmd_argv, NEW.cmd_env)
                || coalesce(E'\ncmd_stderr: ' || convert_from(nullif(NEW.cmd_stderr, ''::bytea), 'UTF-8'),  '')
                || coalesce(E'\ncmd_stdout: ' || convert_from(nullif(NEW.cmd_stdout, ''::bytea), 'UTF-8'),  '')
            )
            ,schema = tg_table_schema
            ,table = tg_table_name
            ,column = 'cmd_term_sig'
        ;
    end if;

    if tg_when = 'BEFORE' then
        return NEW;
    end if;
    return null;
end;
$$;

comment on function nix_queue_cmd__require_exit_success() is
$md$This trigger function will make a ruckus when a command finished with a non-zero exit code or with a termination signal.

`_exit_success()` refers to the `EXIT_SUCCESS` macro defined as an alias for
`0` in `stdlib.h`.
$md$;

--------------------------------------------------------------------------------------------------------------

create function queue_cmd__delete_after_update()
    returns trigger
    language plpgsql
    as $$
declare
    _cmd_id_field name := 'cmd_id';
    _cmd_subid_field name := 'cmd_subid';
begin
    assert tg_when = 'AFTER';
    assert tg_op = 'UPDATE';
    assert tg_level = 'ROW';
    assert tg_nargs in (0, 2);

    if tg_nargs = 2 then
        _cmd_id_field := tg_argv[0];
        _cmd_subid_field := nullif(tg_argv[1], 'null');
    end if;

    execute format(
        'DELETE FROM %s WHERE cmd_id = ($1).%I and cmd_subid IS NOT DISTINCT FROM ($1).%I'
        ,tg_relid::regclass::text
        ,_cmd_id_field
        ,_cmd_subid_field
    ) using OLD;

    return null;
end;
$$;

comment on function queue_cmd__delete_after_update() is
$md$Use this trigger function if the queue holds records that need to deleted on `UPDATE`.

When using a table to hold a queue's commands, you can hook this trigger
function directly to an `ON INSERT` trigger on the `queue_cmd_template`-derived
table itself.  In that case, no arguments are needed.

When the `queue_cmd_template`-derived relation is a view, this trigger function
will have to be attached to the underlying table and will require two
arguments: ① the name of the field which will be mapped to `cmd_id` in the view;
and the name of the field which will be mapped to `cmd_subid` in the view.  If
there is no `cmd_subid`, this third parameter should be `null`.

**Note** that this function is as of yet untested!
$md$;

----------------------------------------------------------------------------------------------------------

-- If the `pg_uuidv7` extension is installed, use this excellent sequentual UUID generation algo.
do $$
declare
    _pg_uuidv7_schema name := (
        select
            extnamespace::regnamespace::text
        from
            pg_extension
        where
            extname = 'pg_uuidv7'
    );
begin
    if _pg_uuidv7_schema is not null then
        perform set_config('search_path', current_setting('search_path') || ',' || _pg_uuidv7_schema, true);
        alter table queue_cmd_template
            alter column cmd_id
                set default uuid_generate_v7();
    end if;
end;
$$;

--------------------------------------------------------------------------------------------------------------

create table nix_queue_cmd_template (
    like queue_cmd_template
       including all
    ,cmd_argv text[]
        not null
        check (array_length(cmd_argv, 1) >= 1)
    ,cmd_env hstore
        not null
        default ''::hstore
    ,cmd_stdin bytea
        not null
        default ''::bytea
    ,cmd_exit_code int
    ,cmd_term_sig int
    ,cmd_stdout bytea
    ,cmd_stderr bytea
    ,constraint check_exited_normally_or_not check (
        num_nonnulls(cmd_exit_code, cmd_term_sig) in (0, 1)
    )
    ,constraint check_finished_in_full check (
        num_nonnulls(
            cmd_runtime
            ,lower(cmd_runtime)
            ,upper(cmd_runtime)
            ,cmd_exit_code  -- We expect either `cmd_exit_code` _or_ `cmd_term_sig`, not both.
            ,cmd_term_sig   -- We expect either `cmd_exit_code` _or_ `cmd_term_sig`, not both.
            ,cmd_stdout
            ,cmd_stderr
        ) in (0, 6)
    )
);

comment on column nix_queue_cmd_template.cmd_term_sig is
$md$If the command exited abnormally, this field should hold the signal with which it exited.

In Unixy systems, a command exits either:
  (a) with an exit code, _or_
  (b) with a termination signal.

Though not all *nix signals are standardized across different Unix variants,
termination signals _are_ part of POSIX; see
[Wikipedia](https://en.wikipedia.org/wiki/Signal_(IPC)#Default_action) and
[GNU](https://www.gnu.org/software/libc/manual/html_node/Termination-Signals.html).
$md$;

create trigger no_insert
    before insert
    on nix_queue_cmd_template
    for each row
    execute function queue_cmd_template__no_insert();

--------------------------------------------------------------------------------------------------------------

create function nix_queue_cmd_template(record)
    returns nix_queue_cmd_template
    immutable
    leakproof
    parallel safe
    set search_path from current
    language plpgsql
    as $$
begin
    return null::nix_queue_cmd_template #= hstore($1);
end;
$$;

--------------------------------------------------------------------------------------------------------------

create function cmd_line(
        cmd_argv$ text[]
        ,cmd_env$ hstore = ''::hstore
        ,cmd_stdin$ bytea = ''
    )
    returns text
    immutable
    leakproof
    parallel safe
    language sql
    return (
        coalesce(
            'echo ' || translate(encode(nullif(cmd_stdin$, ''::bytea), 'base64'), E'\n', '')
                    || ' | base64 -d | '
            ,''
        )
        || coalesce(
            (
                select
                    string_agg(
                        var_name || '=' || case
                            when var_value ~ E'["$ \n\t`]' then
                                '''' || replace(var_value, '''', '''\''''') || ''''
                            else
                                var_value
                        end
                        ,' '
                    )
                from
                   each(cmd_env$) as env_var(var_name, var_value)
            ) || ' '
            ,''
        )
        || (
            select
                string_agg(
                    case
                        when arg ~ E'["$ \n\t`]' then
                            '''' || replace(arg, '''', '''\''''') || ''''
                        else
                            arg
                    end
                    ,' '
                )
            from
                unnest(cmd_argv$) as arg
        )
    );

--------------------------------------------------------------------------------------------------------------

create function cmd_line(nix_queue_cmd_template, bool default false)
    returns text
    immutable
    leakproof
    parallel safe
    set search_path from current
    language sql
    return cmd_line(
        case
            when $2 then
                array[
                    'pg_nix_queue_cmd'
                    ,'--output-update'
                    ,(pg_identify_object('pg_class'::regclass, ($1).cmd_class, 0)).identity
                    ,($1).cmd_id
                    ,($1).cmd_subid
                    ,'--'
                ]
            else
                array[]::text[]
        end || ($1).cmd_argv
        ,($1).cmd_env
        ,($1).cmd_stdin
    );

--------------------------------------------------------------------------------------------------------------

create procedure test__cmd_line()
    set search_path from current
    set plpgsql.check_asserts to true
    language plpgsql
    as $$
declare
    _cmd record;
begin
    create temporary table tst_nix_cmd (
        like nix_queue_cmd_template including all
        ,cmd_line_expected text
            not null
    ) on commit drop;
    alter table tst_nix_cmd
        alter column cmd_class
            set default to_regclass('tst_nix_cmd');
    insert into cmd_queue
        (cmd_class, cmd_signature_class)
    values
        ('tst_nix_cmd', 'nix_queue_cmd_template')
    ;

    with inserted as (
        insert into tst_nix_cmd (cmd_id, cmd_argv, cmd_env, cmd_stdin, cmd_line_expected)
        values (
            'test1'
            ,array['cmd', '--option-1', 'arg with spaces and $ and "', 'arg', 'arg with ''single-quoted'' text']
            ,'VAR1=>"value 1", VAR_TWO=>val2'::hstore
            ,E'Multiline\ntext\n'
            ,$str$echo TXVsdGlsaW5lCnRleHQK | base64 -d | VAR1='value 1' VAR_TWO=val2 cmd --option-1 'arg with spaces and $ and "' arg 'arg with '\''single-quoted'\'' text'$str$
        )
        returning
            *
    )
    select
        inserted.*
        ,cmd_line(inserted::tst_nix_cmd, false) as cmd_line_actual
    from
        inserted
    into
        _cmd
    ;
    assert _cmd.cmd_line_actual = _cmd.cmd_line_expected,
        format(E'\n%s\n≠\n%s', _cmd.cmd_line_actual, _cmd.cmd_line_expected);

    with inserted as (
        insert into tst_nix_cmd (cmd_id, cmd_argv, cmd_env, cmd_stdin, cmd_line_expected)
        values (
            'test2'
            ,array['cmd2', '--opt']
            ,''::hstore
            ,E'Just one line\n'
            ,$str$echo SnVzdCBvbmUgbGluZQo= | base64 -d | pg_nix_queue_cmd --output-update pg_temp.tst_nix_cmd test2 -- cmd2 --opt$str$
        )
        returning
            *
    )
    select
        inserted.*
        ,cmd_line(inserted::tst_nix_cmd, true) as cmd_line_actual
    from
        inserted
    into
        _cmd
    ;
    assert _cmd.cmd_line_actual = _cmd.cmd_line_expected,
        format(E'\n%s\n≠\n%s', _cmd.cmd_line_actual, _cmd.cmd_line_expected);

    with inserted as (
        insert into tst_nix_cmd (cmd_id, cmd_subid, cmd_argv, cmd_env, cmd_stdin, cmd_line_expected)
        values (
            'test2'
            ,'subid'
            ,array['cmd2', '--opt']
            ,''::hstore
            ,E'Just one line\n'
            ,$str$echo SnVzdCBvbmUgbGluZQo= | base64 -d | pg_nix_queue_cmd --output-update pg_temp.tst_nix_cmd test2 subid -- cmd2 --opt$str$
        )
        returning
            *
    )
    select
        inserted.*
        ,cmd_line(inserted::tst_nix_cmd, true) as cmd_line_actual
    from
        inserted
    into
        _cmd
    ;
    assert _cmd.cmd_line_actual = _cmd.cmd_line_expected,
        format(E'\n%s\n≠\n%s', _cmd.cmd_line_actual, _cmd.cmd_line_expected);
end;
$$;

--------------------------------------------------------------------------------------------------------------

create type sql_status_type as enum (
    'PGRES_EMPTY_QUERY'
    ,'PGRES_COMMAND_OK'
    ,'PGRES_TUPLES_OK'
    --,'PGRES_COPY_OUT'  -- irrelevant to pg_cmd_queue
    --,'PGRES_COPY_IN'  -- irrelevant to pg_cmd_queue
    ,'PGRES_BAD_RESPONSE'
    --,'PGRES_NONFATAL_ERROR'  -- only relevant for notices
    ,'PGRES_FATAL_ERROR'
    --,'PGRES_COPY_BOTH'  -- irrelevant to pg_cmd_queue
    --,'PGRES_SINGLE_TUPLE'  -- commented out, because we do not support single row mode
    --,'PGRES_PIPELINE_SYNC'  -- pg_cmd_queue_daemon doesn't use libpq's pipeline mode
    --,'PGRES_PIPELINE_ABORTED'  -- pg_cmd_queue_daemon doesn't use libpq's pipeline mode
);

comment on type sql_status_type is
$md$The possible SQL command result statuses.

[`PQresultStatus()`](https://www.postgresql.org/docs/current/libpq-exec.html#LIBPQ-PQRESULTSTATUS)
$md$;

--------------------------------------------------------------------------------------------------------------

create type sql_errorish as (
    pg_diag_severity text
    ,pg_diag_severity_nonlocalized text
    ,pg_diag_sqlstate text
    ,pg_diag_message_primary text
    ,pg_diag_message_detail text
    ,pg_diag_message_hint text
    ,pg_diag_statement_position text
    ,pg_diag_internal_position text
    ,pg_diag_internal_query text
    ,pg_diag_context text
    ,pg_diag_schema_name text
    ,pg_diag_table_name text
    ,pg_diag_column_name text
    ,pg_diag_datatype_name text
    ,pg_diag_constraint_name text
    ,pg_diag_source_file text
    ,pg_diag_source_line text
    ,pg_diag_source_function text
);

comment on type sql_errorish is
$md$

The field names of this type are the lowercased version of the field codes from
the documentation for libpq's
[`PQresultErrorField()`](https://www.postgresql.org/docs/current/libpq-exec.html#LIBPQ-PQRESULTERRORFIELD)
function.
$md$;

--------------------------------------------------------------------------------------------------------------

create table sql_queue_cmd_template (
    like queue_cmd_template
       including all
    ,cmd_sql text
        not null
    ,cmd_sql_result_status sql_status_type
    ,cmd_sql_fatal_error sql_errorish
    ,cmd_sql_nonfatal_errors sql_errorish[]
    ,cmd_sql_result_rows jsonb
);


comment on column sql_queue_cmd_template.cmd_sql_result_rows is
$md$The result rows represented as a JSON array of objects.

When `cmd_sql` produced no rows, `cmd_sql` will contain either an SQL `NULL` or
JSON `'null'` value.
$md$;


create trigger no_insert
    before insert
    on sql_queue_cmd_template
    for each row
    execute function queue_cmd_template__no_insert();

--------------------------------------------------------------------------------------------------------------

create function sql_queue_cmd__require_status_ok()
    returns trigger
    language plpgsql
    as $$
begin
    assert tg_when in ('BEFORE', 'AFTER');
    assert tg_op = 'UPDATE';
    assert tg_level = 'ROW';

    -- TODO: Implement check

    if tg_when = 'BEFORE' then
        return NEW;
    end if;
    return null;
end;
$$;

--------------------------------------------------------------------------------------------------------------

create table http_queue_cmd_template (
    like queue_cmd_template
    ,cmd_http_url text
    ,cmd_http_version text
    ,cmd_http_method text
    ,cmd_http_request_headers hstore
    ,cmd_http_request_body bytea
    ,cmd_http_response_headers hstore
    ,cmd_http_response_body bytea
);

--------------------------------------------------------------------------------------------------------------

create schema cmdqd;

comment on schema cmdqd is
$md$`pg_cmdqd` has to access the `cmdq` schema exclusively through this application-specific schema (ASS).
$md$;

--------------------------------------------------------------------------------------------------------------

create view cmdqd.cmd_queue as
select
    q.cmd_class
    ,quote_ident(pg_namespace.nspname) || '.' || quote_ident(pg_class.relname) as cmd_class_identity
    ,pg_class.relname as cmd_class_relname
    ,q.cmd_signature_class
    ,(parse_ident(q.cmd_signature_class::regclass::text))[
        array_upper(parse_ident(q.cmd_signature_class::regclass::text), 1)
    ] AS cmd_signature_class_relname
    ,q.queue_runner_role
    ,q.queue_notify_channel
    ,extract('epoch' from q.queue_reselect_interval) * 10^3 AS queue_reselect_interval_msec
    ,q.queue_reselect_randomized_every_nth
    ,extract('epoch' from q.queue_select_timeout) as queue_select_timeout_sec
    ,extract('epoch' from q.queue_cmd_timeout) AS queue_cmd_timeout_sec
    ,q.queue_metadata_updated_at
    ,color.ansi_fg
from
    cmdq.cmd_queue as q
inner join
    pg_catalog.pg_class
    on pg_class.oid = q.cmd_class
inner join
    pg_catalog.pg_namespace
    on pg_namespace.oid = pg_class.relnamespace
cross join lateral
    cmdq.cmd_class_color(cmd_class) as color
where
    q.queue_is_enabled
;

--------------------------------------------------------------------------------------------------------------

create function cmdqd.select_cmd_from_queue_stmt(
        cmd_queue$ cmdqd.cmd_queue
        ,where_condition$ text
        ,order_by_expression$ text
    )
    returns text
    immutable
    leakproof
    parallel safe
    language sql
    return
'SELECT
    (pg_identify_object(''pg_class''::regclass, cmd_class, 0)).identity AS cmd_class_identity
    ,(parse_ident(cmd_class::text))[
        array_upper(parse_ident(cmd_class::text), 1)
    ] AS cmd_class_relname
    ,cmd_id
    ,cmd_subid
    ,extract(epoch from cmd_queued_since) AS cmd_queued_since' || case
when ($1).cmd_signature_class = 'cmdq.sql_queue_cmd_template'::regclass then '
    ,cmd_sql'
when ($1).cmd_signature_class = 'cmdq.nix_queue_cmd_template'::regclass then '
    ,cmd_argv
    ,cmd_env
    ,convert_from(cmd_stdin, ''UTF8'') AS cmd_stdin'  -- FIXME: We shouldn't assume text, let alone UTF-8.
when ($1).cmd_signature_class = 'cmdq.http_queue_cmd_template'::regclass then '
    ,cmd_http_url text
    ,cmd_http_version text
    ,cmd_http_method text
    ,cmd_http_request_headers hstore
    ,cmd_http_request_body bytea' end || '
FROM
    ' || ($1).cmd_class::text || ' AS q
WHERE
    NOT EXISTS (
        SELECT FROM
            updated_cmd AS u
        WHERE
            u.cmd_id = q.cmd_id
            AND u.cmd_subid IS NOT DISTINCT FROM q.cmd_subid
    )
    ' || coalesce('AND (
        ' || where_condition$ || '
    )',  '') || '
' || coalesce('
ORDER BY
    ' || order_by_expression$ || '
', '') || '
LIMIT 1
FOR UPDATE OF q SKIP LOCKED
';

--------------------------------------------------------------------------------------------------------------

create procedure cmdqd.prepare_to_select_cmd_from_queue(cmdqd.cmd_queue)
    language plpgsql
    as $$
begin
    execute 'PREPARE select_oldest_cmd AS '
        || cmdqd.select_cmd_from_queue_stmt($1, null, 'cmd_queued_since');
    execute 'PREPARE select_random_cmd AS '
        || cmdqd.select_cmd_from_queue_stmt($1, null, 'random()');
    execute 'PREPARE select_notify_cmd AS '
        || cmdqd.select_cmd_from_queue_stmt($1, 'cmd_id = $1 AND cmd_subid IS NOT DISTINCT FROM $2', null);
end;
$$;

--------------------------------------------------------------------------------------------------------------

create function cmdqd.update_cmd_in_queue_stmt(cmdqd.cmd_queue)
    returns text
    immutable
    leakproof
    parallel safe
    language sql
    return
'WITH updated_cmd_cte AS (
    UPDATE
        ' || ($1).cmd_class::text || '
    SET
        cmd_runtime = tstzrange(to_timestamp($3), to_timestamp($4))' || case
when ($1).cmd_signature_class = 'cmdq.sql_queue_cmd_template'::regclass then '
        ,cmd_sql_result_status = $5
        ,cmd_sql_result_rows = $6
        ,cmd_sql_fatal_error = $7
        ,cmd_sql_nonfatal_errors = $8'
when ($1).cmd_signature_class = 'cmdq.nix_queue_cmd_template'::regclass then '
        ,cmd_exit_code = $5
        ,cmd_term_sig = $6
        ,cmd_stdout = $7
        ,cmd_stderr = $8'
when ($1).cmd_signature_class = 'cmdq.http_queue_cmd_template'::regclass then '
        ,cmd_http_response_headers = $5
        ,cmd_http_response_body = $6' end || '
    WHERE
        cmd_id = $1
        AND cmd_subid IS NOT DISTINCT from $2
    RETURNING
        cmd_id
        ,cmd_subid
)
INSERT INTO updated_cmd (
    cmd_id
    ,cmd_subid
)
SELECT
    cmd_id
    ,cmd_subid
FROM
    updated_cmd_cte
';

--------------------------------------------------------------------------------------------------------------

create procedure cmdqd.prepare_to_update_cmd_in_queue(cmdqd.cmd_queue)
    language plpgsql
    as $$
begin
    execute 'PREPARE update_cmd AS ' || cmdqd.update_cmd_in_queue_stmt($1);
end;
$$;

--------------------------------------------------------------------------------------------------------------

create procedure cmdqd.runner_session_start(cmdqd.cmd_queue)
    set search_path to pg_catalog
    language plpgsql
    as $$
begin
    if ($1).queue_runner_role is not null then
        -- TODO
        --execute format('SET SESSION ROLE %I', ($1).queue_runner_role);
    end if;

    perform set_config('pg_cmd_queue.runner.reselect_round', '0', false);

    create temporary table updated_cmd (
        cmd_id text
            not null
        ,cmd_subid text
        ,unique nulls not distinct (cmd_id, cmd_subid)
    );

    call cmdqd.prepare_to_select_cmd_from_queue($1);
    call cmdqd.prepare_to_update_cmd_in_queue($1);

    -- Let the listener(s) know that the daemon is about to start selecting things from the queue:
    perform pg_notify(
        cmdq.pg_cmd_queue_notify_channel(),
        row('cmdq.cmd_queue', ($1).cmd_class_identity, 'PREPARE')::text
    );

    if ($1).queue_notify_channel is not null then
        execute format('LISTEN %I', ($1).queue_notify_channel);

        -- Let everybody (interested) know that we're `LISTEN`ing:
        perform pg_notify(
            cmdq.pg_cmd_queue_notify_channel(),
            row('cmdq.cmd_queue', ($1).cmd_class_identity, 'PREPARE')::text
        );
    end if;
end;
$$;

--------------------------------------------------------------------------------------------------------------

create procedure cmdqd.runner_session_start(regclass)
    language plpgsql
    as $$
declare
    _q cmdqd.cmd_queue := (select row(q.*)::cmdqd.cmd_queue from cmdqd.cmd_queue as q where q.cmd_class = $1);
begin
    call cmdqd.runner_session_start(_q);
end;
$$;

--------------------------------------------------------------------------------------------------------------

create function cmdqd.enter_reselect_round()
    returns table (
        reselect_round bigint
    )
    set search_path to pg_catalog
    language plpgsql
    as $$
declare
    _reselect_round bigint := current_setting('pg_cmd_queue.runner.reselect_round')::bigint;
begin
    truncate updated_cmd;

    if _reselect_round = 9223372036854775807 then
        _reselect_round := 0;  -- Wrap around when we reached the max size of `bigint`.
    end if;
    perform set_config('pg_cmd_queue.runner.reselect_round', _reselect_round + 1);

    return row(_reselect_round);
end;
$$;

--------------------------------------------------------------------------------------------------------------

create procedure cmdqd.remember_failed_update_for_this_reselect_round(cmd_id$ text, cmd_subid$ text = null)
    language plpgsql
    as $$
begin
    assert pg_current_xact_id_if_assigned() is null,
        'The `ROLLBACK` after the failed `UPDATE` should have already happened prior to calling this proc.';

    insert into updated_cmd (
        cmd_id
        ,cmd_subid
    )
    values (
        cmd_id$
        ,cmd_subid$
    );
end;
$$;

--------------------------------------------------------------------------------------------------------------

create procedure test__pg_cmd_queue()
    set search_path from current
    set plpgsql.check_asserts to true
    language plpgsql
    as $$
declare
    _user1 record;
    _user2 record;
begin
    -- Googling for “wobbie” suggests that namespace collisions will be unlikely.
    create schema wobbie;  -- “Wobbie is like Facebook, but for Wobles!”
    perform set_config('search_path', 'wobbie,' || current_setting('search_path'), true);
    assert current_schema = 'wobbie';

    -- We want to have a mockable `now()` (without introducing a dependency on `pg_mockable`).
    create or replace function fake_now()
        returns timestamptz
        immutable
        return '2023-07-24 07:00'::timestamptz;

    create table wobbie_user (
        user_id uuid
            not null
            default gen_random_uuid()
            -- In most real-world scenarios, you will want to use UUIDv7
            -- instead; see:
            -- https://blog.bigsmoke.us/2023/06/04/postgresql-sequential-uuids
        ,created_at timestamptz
            not null
            default fake_now()
        ,email text
            not null
            unique
        ,email_verification_token uuid
            default gen_random_uuid()
        ,email_verification_mail_sent_at timestamptz
        ,email_verified_at timestamptz
        ,password text  -- Wobbie trusts their users to trust them with plain text passwords!
            not null
        ,password_reset_requested_at timestamptz
        ,password_reset_mail_sent_at timestamptz
        ,password_reset_token uuid
            unique
    );

    create view cmdq.wobbie_user_confirmation_mail_cmd
    as
    select
        -- Outside of a PL/pgSQL context, you can just do `'ns.relation'::regclass`.
        to_regclass('cmdq.wobbie_user_confirmation_mail_cmd')
        ,u.user_id::text as cmd_id
        ,null::text as cmd_subid
        ,u.created_at as cmd_queued_since
        ,null::tstzrange as cmd_runtime
        ,array[
            'wobbie-mail'
            ,'--to'
            ,u.email
            ,'--subject'
            ,'Confirm your new account at Wobbie'
            ,'--template'
            ,'confirm-email.html'
        ] as cmd_argv
        ,hstore(
            'WOBBIE_SMTP_HOST', '127.0.0.1'
        ) as cmd_env
        ,to_json(u.*)::text::bytea as cmd_stdin
        ,null::int as cmd_exit_code
        ,null::int as cmd_term_sig
        ,null::bytea as cmd_stdout
        ,null::bytea as cmd_stderrr  -- One r too many.
    from
        wobbie_user as u
    where
        u.email_verified_at is null
        and u.email_verification_mail_sent_at is null
    ;

    <<incompatible_view_signature>>
    begin
        insert into cmd_queue (
            cmd_class
            ,cmd_signature_class
            ,queue_reselect_interval
            ,queue_wait_time_limit_warn
            ,queue_wait_time_limit_crit
        )
        values (
            'cmdq.wobbie_user_confirmation_mail_cmd'
            ,'nix_queue_cmd_template'
            ,'1 minute'::interval
            ,'2 minutes'::interval
            ,'5 minutes'::interval
        );

        raise assert_failure using
            message = 'Should not be able to register view with incompatible signature.';
    exception
        when integrity_constraint_violation then
    end incompatible_view_signature;

    alter view cmdq.wobbie_user_confirmation_mail_cmd
        rename column cmd_stderrr to cmd_stderr;

    create function cmdq.wobbie_user_confirmation_mail_cmd__instead_of_update()
        returns trigger
        language plpgsql
        as $plpgsql$
    begin
        assert tg_when = 'INSTEAD OF';
        assert tg_op = 'UPDATE';
        assert tg_level = 'ROW';
        assert tg_table_schema = 'cmdq';
        assert tg_table_name = 'wobbie_user_confirmation_mail_cmd';

        if NEW.cmd_exit_code > 0 then
            raise exception using message = format(
                E'%s returned a non-zero exit code (%s); stdout: %s\n\nstderr: %s'
                ,array_to_string(OLD.cmd_argv, ' ')
                ,NEW.cmd_exit_code
                ,NEW.cmd_stdout
                ,NEW.cmd_stderr
            );
        end if;

        update
            wobbie_user
        set
            email_verification_mail_sent_at = fake_now()
        where
            user_id = OLD.cmd_id::uuid
        ;

        return NEW;
    end;
    $plpgsql$;

    create trigger instead_of_update
        instead of update
        on cmdq.wobbie_user_confirmation_mail_cmd
        for each row
        execute function cmdq.wobbie_user_confirmation_mail_cmd__instead_of_update();

    create view cmdq.wobbie_user_password_reset_mail_cmd
    as
    select
        -- Outside of a PL/pgSQL context, you can just do `'ns.relation'::regclass`.
        to_regclass('cmdq.wobbie_email_password_reset_mail_cmd')
        ,u.user_id::text as cmd_id
        ,null::text as cmd_subid
        ,u.password_reset_requested_at as cmd_queued_since
        ,null::tstzrange as cmd_runtime
        ,array[
            'wobbie-mail'
            ,'--to'
            ,u.email
            ,'--subject'
            ,'Someone (probably you) requested to reset your Wobbie password'
            ,'--template'
            ,'password-reset-mail.html'
        ] as cmd_argv
        ,hstore(
            'WOBBIE_SMTP_HOST', '127.0.0.1'
        ) as cmd_env
        ,to_json(u.*)::text::bytea as cmd_stdin
        ,null::int as cmd_exit_code
        ,null::int as cmd_term_sig
        ,null::bytea as cmd_stdout
        ,null::bytea as cmd_stderr
    from
        wobbie_user as u
    where
        u.password_reset_requested_at is not null
        and u.password_reset_mail_sent_at is null
    ;

    create function cmdq.wobbie_user_password_reset_mail_cmd__instead_of_update()
        returns trigger
        language plpgsql
        as $plpgsql$
    begin
        assert tg_when = 'INSTEAD OF';
        assert tg_op = 'UPDATE';
        assert tg_level = 'ROW';
        assert tg_table_schema = 'cmdq';
        assert tg_table_name = 'wobbie_user_password_reset_mail_cmd';

        if NEW.cmd_exit_code > 0 then
            raise exception using message = format(
                E'%s returned a non-zero exit code (%s); stdout: %s\n\nstderr: %s'
                ,array_to_string(OLD.cmd_argv, ' ')
                ,NEW.cmd_exit_code
                ,NEW.cmd_stdout
                ,NEW.cmd_stderr
            );
        end if;

        update
            wobbie_user
        set
            password_reset_mail_sent_at = fake_now()
        where
            user_id = OLD.cmd_id::uuid
        ;
        assert found;

        return NEW;
    end;
    $plpgsql$;

    create trigger instead_of_update
        instead of update
        on cmdq.wobbie_user_password_reset_mail_cmd
        for each row
        execute function cmdq.wobbie_user_password_reset_mail_cmd__instead_of_update();

    insert into cmd_queue (
        cmd_class
        ,cmd_signature_class
        ,queue_reselect_interval
        ,queue_wait_time_limit_warn
        ,queue_wait_time_limit_crit
    )
    values (
        'cmdq.wobbie_user_confirmation_mail_cmd'
        ,'nix_queue_cmd_template'
        ,'1 minute'::interval
        ,'2 minutes'::interval
        ,'5 minutes'::interval
    )
    ,(
        'cmdq.wobbie_user_password_reset_mail_cmd'
        ,'nix_queue_cmd_template'
        ,'2 minutes'::interval
        ,'5 minutes'::interval
        ,'8 minutes'::interval
    );

    insert into wobbie_user (
        email
        ,password
    )
    values (
        'somebody@example.com'
        ,'supers3cur3'
    )
    returning
        *
    into
        _user1
    ;

    assert (select count(*) from cmdq.wobbie_user_confirmation_mail_cmd) = 1;
    assert (select count(*) from cmdq.wobbie_user_password_reset_mail_cmd) = 0;

    -- Pretend that `pg_cmd_queue_runnerd` has come along, spawned the command and is reporting the result.
    update
        cmdq.wobbie_user_confirmation_mail_cmd
    set
        cmd_exit_code = 0
        ,cmd_runtime = tstzrange(fake_now() + '1 second'::interval, fake_now() + '2s30ms'::interval)
        ,cmd_stdout = E'Mail sent delivered successfully to mta.example.com\n'
        ,cmd_stderr = ''
    where
        cmd_id = _user1.user_id::text
    ;
    assert found;

    assert (select count(*) from cmdq.wobbie_user_confirmation_mail_cmd) = 0;
    assert (select count(*) from cmdq.wobbie_user_password_reset_mail_cmd) = 0;

    create or replace function fake_now()
        returns timestamptz
        immutable
        return '2023-07-25 09:00'::timestamptz;

    -- Pretend that the user has requested a password reset in the Wobbie website.
    update
        wobbie_user
    set
        password_reset_requested_at = fake_now()
    where
        user_id = _user1.user_id
    ;

    assert (select count(*) from cmdq.wobbie_user_confirmation_mail_cmd) = 0;
    assert (select count(*) from cmdq.wobbie_user_password_reset_mail_cmd) = 1;

    -- Pretend that `pg_cmd_queue_runnerd` has come along, spawned the command and is reporting the result.
    update
        cmdq.wobbie_user_password_reset_mail_cmd
    set
        cmd_exit_code = 0
        ,cmd_runtime = tstzrange(fake_now() + '3 second'::interval, fake_now() + '3s640ms'::interval)
        ,cmd_stdout = E'Mail sent delivered successfully to mta.example.com\n'
        ,cmd_stderr = ''
    where
        cmd_id = _user1.user_id::text
    ;
    assert found;

    assert (select count(*) from cmdq.wobbie_user_confirmation_mail_cmd) = 0;
    assert (select count(*) from cmdq.wobbie_user_password_reset_mail_cmd) = 0;

    raise transaction_rollback;
exception
    when transaction_rollback then
end;
$$;

----------------------------------------------------------------------------------------------------------

create procedure test_dump_restore__pg_cmd_queue(test_stage$ text)
    set search_path from current
    set plpgsql.check_asserts to true
    language plpgsql
    as $$
begin
    assert test_stage$ in ('pre-dump', 'post-restore');

    if test_stage$ = 'pre-dump' then
        -- Googling for “wobbie” suggests that namespace collisions will be unlikely.
        create schema wobbie;  -- “Wobbie is like Facebook, but for Wobles!”
        perform set_config('search_path', 'wobbie,' || current_setting('search_path'), true);
        assert current_schema = 'wobbie';

        -- We want to have a mockable `now()` (without introducing a dependency on `pg_mockable`).
        create or replace function fake_now()
            returns timestamptz
            immutable
            return '2023-07-24 07:00'::timestamptz;

        create table wobbie_user (
            user_id uuid
                not null
                default gen_random_uuid()
                -- In most real-world scenarios, you will want to use UUIDv7
                -- instead; see:
                -- https://blog.bigsmoke.us/2023/06/04/postgresql-sequential-uuids
            ,created_at timestamptz
                not null
                default fake_now()
            ,email text
                not null
                unique
            ,email_verification_token uuid
                default gen_random_uuid()
            ,email_verification_mail_sent_at timestamptz
            ,email_verified_at timestamptz
            ,password text  -- Wobbie trusts their users to trust them with plain text passwords!
                not null
            ,password_reset_requested_at timestamptz
            ,password_reset_mail_sent_at timestamptz
            ,password_reset_token uuid
                unique
        );

        create view cmdq.wobbie_user_confirmation_mail_cmd
        as
        select
            -- Outside of a PL/pgSQL context, you can just do `'ns.relation'::regclass`.
            to_regclass('cmdq.wobbie_user_confirmation_mail_cmd')
            ,u.user_id::text as cmd_id
            ,null::text as cmd_subid
            ,u.created_at as cmd_queued_since
            ,null::tstzrange as cmd_runtime
            ,array[
                'wobbie-mail'
                ,'--to'
                ,u.email
                ,'--subject'
                ,'Confirm your new account at Wobbie'
                ,'--template'
                ,'confirm-email.html'
            ] as cmd_argv
            ,hstore(
                'WOBBIE_SMTP_HOST', '127.0.0.1'
            ) as cmd_env
            ,to_json(u.*)::text::bytea as cmd_stdin
            ,null::smallint as cmd_exit_code
            ,null::bytea as cmd_stdout
            ,null::bytea as cmd_stderr
        from
            wobbie_user as u
        where
            u.email_verified_at is null
            and u.email_verification_mail_sent_at is null
        ;

        create function cmdq.wobbie_user_confirmation_mail_cmd__instead_of_update()
            returns trigger
            language plpgsql
            as $plpgsql$
        begin
            assert tg_when = 'INSTEAD OF';
            assert tg_op = 'UPDATE';
            assert tg_level = 'ROW';
            assert tg_table_schema = 'cmdq';
            assert tg_table_name = 'wobbie_user_confirmation_mail_cmd';

            if NEW.cmd_exit_code > 0 then
                raise exception using message = format(
                    E'%s returned a non-zero exit code (%s); stdout: %s\n\nstderr: %s'
                    ,array_to_string(OLD.cmd_argv, ' ')
                    ,NEW.cmd_exit_code
                    ,NEW.cmd_stdout
                    ,NEW.cmd_stderr
                );
            end if;

            update
                wobbie_user
            set
                email_verification_mail_sent_at = fake_now()
            where
                user_id = OLD.cmd_id::uuid
            ;

            return NEW;
        end;
        $plpgsql$;

        create trigger instead_of_update
            instead of update
            on cmdq.wobbie_user_confirmation_mail_cmd
            for each row
            execute function cmdq.wobbie_user_confirmation_mail_cmd__instead_of_update();

        insert into cmd_queue (
            cmd_class
            ,cmd_signature_class
            ,queue_reselect_interval
            ,queue_wait_time_limit_warn
            ,queue_wait_time_limit_crit
        )
        values (
            'cmdq.wobbie_user_confirmation_mail_cmd'
            ,'nix_queue_cmd_template'
            ,'1 minute'::interval
            ,'2 minutes'::interval
            ,'5 minutes'::interval
        );

    elsif test_stage$ = 'post-restore' then
        assert (select count(*) from cmdq.cmd_queue) = 1;
    end if;
end;
$$;

--------------------------------------------------------------------------------------------------------------

create procedure assert_queue_cmd_run_result(
        cmd_log_class$ regclass
        ,expect$ nix_queue_cmd_template
        ,cmdqd_timeout$ interval = '10 seconds'::interval
    )
    language plpgsql
    as $$
declare
    _wait_start timestamptz := clock_timestamp();
    _actual record;
    _errors text[] = array[]::text[];
    _cmd_log_ident text := cmd_log_class$::regclass::text;
begin
    <<wait_for_cmdqd>>
    loop
        rollback and chain;  -- Because otherwise, we won't see the changes made by the `pg_cmdqd`

        execute format($sql$
            SELECT
                *
            FROM
                %s
            WHERE
                cmd_runtime IS NOT NULL
                AND cmd_id = ($1).cmd_id
                AND cmd_subid IS NOT DISTINCT FROM ($1).cmd_subid
            $sql$, _cmd_log_ident
        ) into _actual using expect$;

        exit wait_for_cmdqd when _actual.cmd_runtime is not null;

        if clock_timestamp() - _wait_start > cmdqd_timeout$ then
            raise assert_failure using message = format(
                'Waited > %s for pg_cmdqd to run %s', cmdqd_timeout$, expect$.cmd_id
            );
        end if;
        perform pg_sleep(0.001);  -- seconds
    end loop wait_for_cmdqd;

    if _actual.cmd_exit_code is distinct from expect$.cmd_exit_code then
        _errors := _errors || format('cmd_exit_code = %s ≠ %s', _actual.cmd_exit_code, expect$.cmd_exit_code);
    end if;
    if _actual.cmd_term_sig is distinct from expect$.cmd_term_sig then
        _errors := _errors || format(
            'cmd_term_sig = %s ≠ %s'
            ,coalesce(_actual.cmd_term_sig::text, 'NULL')
            ,coalesce(expect$.cmd_term_sig::text, 'NULL')
        );
    end if;
    if _actual.cmd_stdout is distinct from expect$.cmd_stdout then
        _errors := _errors || format(E'cmd_stdout = %L\n≠ %L', convert_from(_actual.cmd_stdout, 'UTF8'), convert_from(expect$.cmd_stdout, 'UTF8'));
    end if;
    if _actual.cmd_stderr is distinct from expect$.cmd_stderr then
        _errors := _errors || format(E'cmd_stderr = %L\n≠ %L', convert_from(_actual.cmd_stderr, 'UTF8'), convert_from(expect$.cmd_stderr, 'UTF8'));
    end if;

    if array_length(_errors, 1) > 0 then
        raise assert_failure using
            message = array_to_string(_errors, E'\n')
            ,detail = format(
                'WHERE cmd_id = %L AND cmd_sub_id IS NOT DISTINCT FROM %s'
                ,(expect$).cmd_id
                ,coalesce(quote_literal((expect$).cmd_subid), 'NULL')
            )
            ,hint = format('cmd_runtime = %s', _actual.cmd_runtime);
    end if;
end;
$$;

--------------------------------------------------------------------------------------------------------------

create procedure test_integration__pg_cmdqd(test_stage$ text)
    language plpgsql
    as $$
declare
    _expect record;
    _actual record;
    _valid_test_stages constant text[] := array['setup', 'test', 'teardown'];
begin
    -- Because this procedure executes transaction control statements, we cannot attach these as `SET`
    -- clauses to the `CREATE PROCEDURE` statement.  Let's bluntly override the session-level settings.
    set session plpgsql.check_asserts to true;
    set session pg_pure_tests.require_extra_daemon to 'pg_cmdqd';
    perform set_config('search_path', cmdq.pg_cmd_queue_search_path(), false);

    assert exists (select from pg_catalog.pg_stat_activity where application_name = 'pg_cmdqd'),
        '`pg_cmdqd` needs to be connected to the database.';

    assert test_stage$ = any (_valid_test_stages), format(
        'test_stage$ = %1$L; %1$L NOT IN (%2$s)'
        ,test_stage$
        ,(select string_agg(quote_literal(s), ', ') from unnest(_valid_test_stages) as s)
    );

    if test_stage$ = 'setup' then
        create table tst_nix_cmd (
            like nix_queue_cmd_template
                including all
        );
        alter table tst_nix_cmd
            alter column cmd_class set default 'tst_nix_cmd';

        create table tst_nix_cmd__expect (
            like tst_nix_cmd
                including all excluding constraints
        );
        create table tst_nix_cmd__actual (
            like tst_nix_cmd
                including all
        );

        create trigger notify
            after insert
            on tst_nix_cmd
            for each row
            execute function queue_cmd__notify('tst_nix_cmd');

        create trigger insert_elsewhere_before_update
            before update
            on tst_nix_cmd
            for each row
            execute function queue_cmd__insert_elsewhere('cmdq.tst_nix_cmd__actual');

        create trigger delete_after_update
            after update
            on tst_nix_cmd
            for each row
            execute function queue_cmd__delete_after_update();

        create role cmdq_test_role;

        insert into tst_nix_cmd__expect (
            cmd_id
            ,cmd_subid
            ,cmd_argv
            ,cmd_env
            ,cmd_stdin
            ,cmd_exit_code
            ,cmd_term_sig
            ,cmd_stdout
            ,cmd_stderr
        )
        values (
            'cmd-with-clean-exit'
            ,null
            ,array[
                'nixtestcmd'
                ,'--stdout-line'
                ,'This line should be sent to STDOUT.'
                ,'--stderr-line'
                ,'This line is to be sent to STDERR.'
                ,'--echo-stdin'
                ,'--stdout-line'
                ,'This line should be printed after the echoed STDIN.'
                ,'--echo-env-var'
                ,'PG_CMDQ_TST_VAR1'
                ,'--stdout-line'
                ,'This line should be squeezed between 2 env. variables.'
                ,'--echo-env-var'
                ,'PG_CMDQ_TST_VAR2'
                ,'--exit-code'
                ,'0'
            ]
            ,'PG_CMDQ_TST_VAR1=>var1_value,PG_CMDQ_TST_VAR2=>"var2: with => signs, \\ and \""'::hstore
            ,E'This STDIN should be echoed to STDOUT,\nincluding this 2nd line.\n'::bytea
            ,0
            ,null
            ,convert_to($out$This line should be sent to STDOUT.
This STDIN should be echoed to STDOUT,
including this 2nd line.
This line should be printed after the echoed STDIN.
var1_value
This line should be squeezed between 2 env. variables.
var2: with => signs, \ and "
$out$, 'UTF8')
            ,convert_to(E'This line is to be sent to STDERR.\n', 'UTF8')
        )
        ,(
            'cmd-with-funky-characters-in-argv'
            ,null
            ,array[
                'nixtestcmd'
                ,'--stdout-line'
                ,'Let us check that `pg_cmdqd` unescapes quotes: """.'
                ,'--exit-code'
                ,'0'
            ]
            ,''::hstore
            ,''::bytea
            ,0
            ,null
            ,convert_to(E'Let us check that `pg_cmdqd` unescapes quotes: """.\n', 'UTF8')
            ,convert_to('', 'UTF8')
        )
        ,(
            'cmd-exceeds-timeout-with-default-sigterm-action'
            ,null
            ,array[
                'nixtestcmd'
                ,'--stdout-line'
                ,'Line 1.'
                ,'--sleep-ms'
                ,'10000'
                ,'--exit-code'
                ,'0'
            ]
            ,''::hstore
            ,''::bytea
            ,null
            ,15  -- Signal 15 = SIGTERM
            ,convert_to(E'Line 1.\n', 'UTF8')
            ,convert_to('', 'UTF8')
        )
        ,(
            'cmd-exceeds-timeout-but-exits-cleanly-on-sigterm'
            ,null
            ,array[
                'nixtestcmd'
                ,'--stdout-line'
                ,'Line 1.'
                ,'--exit-code-on-sigterm'
                ,'15'  -- Hehehe
                ,'--sleep-ms'
                ,'10000'
                ,'--exit-code'
                ,'0'
            ]
            ,''::hstore
            ,''::bytea
            ,15
            ,null
            ,convert_to(E'Line 1.\n', 'UTF8')
            ,convert_to('', 'UTF8')
        )
        ,(
            'cmd-exceeds-timeout-and-ignores-sigterm'
            ,null
            ,array[
                'nixtestcmd'
                ,'--stdout-line'
                ,'Line 1.'
                ,'--ignore-sigterm'
                ,'--sleep-ms'
                ,'10000'
                ,'--exit-code'
                ,'0'
            ]
            ,''::hstore
            ,''::bytea
            ,null
            ,9  -- Signal 9 = SIGKILL
            ,convert_to(E'Line 1.\n', 'UTF8')
            ,convert_to('', 'UTF8')
        )
        ,(
            'cmd-1-inserted-in-queue-before-queue-is-registered'
            ,null
            ,array['nixtestcmd', '--stdout-line', 'A line', '--exit-code', '0']
            ,''::hstore
            ,''::bytea
            ,0
            ,null
            ,E'A line\n'::bytea
            ,E''::bytea
        )
        ,(
            'cmd-2-inserted-in-queue-before-queue-is-registered'
            ,null
            ,array['nixtestcmd', '--stdout-line', 'A line, distinguishable from cmd-1''s line.', '--exit-code', '0']
            ,''::hstore
            ,''::bytea
            ,0
            ,null
            ,E'A line, distinguishable from cmd-1''s line.\n'::bytea
            ,E''::bytea
        );
    elsif test_stage$ = 'test' then
        <<insert_in_queue_before_registering_queue>>
        declare
            _num_inserted int;
        begin
            with inserted as (
                insert into cmdq.tst_nix_cmd
                    (cmd_id, cmd_subid, cmd_argv, cmd_env, cmd_stdin)
                select
                    e.cmd_id, e.cmd_subid, e.cmd_argv, e.cmd_env, e.cmd_stdin
                from
                    cmdq.tst_nix_cmd__expect as e
                where
                    e.cmd_id ~ '^cmd-[12]-inserted-in-queue-before-queue-is-registered$'
                returning
                    true
            )
            select count(*) from inserted into _num_inserted;

            assert _num_inserted = 2;
        end insert_in_queue_before_registering_queue;

        insert into cmd_queue (
            cmd_class
            ,cmd_signature_class
            ,queue_runner_role
            ,queue_notify_channel
            ,queue_reselect_interval
            ,queue_cmd_timeout
        )
        values (
            'tst_nix_cmd'
            ,'nix_queue_cmd_template'
            --,'cmdq_test_role'
            ,null
            ,'tst_nix_cmd'
            ,'1 day'::interval  -- Let's make sure that we're testing the event stuff for real
            ,'2 second'::interval
        );

        <<check_if_pre_inserted_commands_are_run>>
        declare
            _expect_1 record;
            _expect_2 record;
        begin
            select
                *
            into
                _expect_1
            from
                cmdq.tst_nix_cmd__expect
            where
                cmd_id = 'cmd-1-inserted-in-queue-before-queue-is-registered'
            ;
            select
                *
            into
                _expect_2
            from
                cmdq.tst_nix_cmd__expect
            where
                cmd_id = 'cmd-2-inserted-in-queue-before-queue-is-registered'
            ;

            commit and chain;  -- Because otherwise, our changes will be invisible to the daemon.

            call cmdq.assert_queue_cmd_run_result('cmdq.tst_nix_cmd__actual', cmdq.nix_queue_cmd_template(_expect_1));
            call cmdq.assert_queue_cmd_run_result('cmdq.tst_nix_cmd__actual', cmdq.nix_queue_cmd_template(_expect_2));

            delete from
                cmdq.tst_nix_cmd__expect
            where
                cmd_id ~ '^cmd-[12]-inserted-in-queue-before-queue-is-registered$'
            ;
        end check_if_pre_inserted_commands_are_run;

        <<single_cmd>>
        for _expect in select * from cmdq.tst_nix_cmd__expect loop
            insert into cmdq.tst_nix_cmd
                (cmd_id, cmd_subid, cmd_argv, cmd_env, cmd_stdin)
            values
                (_expect.cmd_id, _expect.cmd_subid, _expect.cmd_argv, _expect.cmd_env, _expect.cmd_stdin)
            ;

            commit and chain;  -- Because otherwise, our changes will be invisible to the daemon.

            -- `tst_nix_cmd__actual` is not the actual command queue table; it is the table to which commands are
            -- moved to by the `UPDATE` triggers _on_ the actual command queue table.
            call cmdq.assert_queue_cmd_run_result('cmdq.tst_nix_cmd__actual', cmdq.nix_queue_cmd_template(_expect));
        end loop single_cmd;

    elsif test_stage$ = 'teardown' then
        delete from cmd_queue where cmd_class = 'cmdq.tst_nix_cmd'::regclass;
        drop role cmdq_test_role;
        drop table tst_nix_cmd cascade;
        drop table tst_nix_cmd__actual cascade;
        drop table tst_nix_cmd__expect cascade;
    end if;
end;
$$;

----------------------------------------------------------------------------------------------------------

create procedure run_sql_cmd_queue(
        cmd_class$ regclass
        ,max_iterations$ bigint default null
        ,iterate_until_empty$ bool default true
        ,queue_reselect_interval$ interval default null
        ,commit_between_iterations$ bool default false
        ,lock_rows_for_update$ bool default true
    )
    set search_path from current
    language plpgsql
    as $$
declare
    _old_role name;
    _old_timeout_ms int;
    _cmd_timeout_ms int;
    _select_timeout_ms int;
    _cmd_queue cmd_queue;
    _cmd_start_time timestamptz;
    _sql_queue_cmd record;
    _sql_queue_cmd_count bigint;
    _cmd_sql_result_status sql_status_type;
    _cmd_sql_result_row record;
    _cmd_sql_result_row_count bigint;
    _cmd_sql_result_rows jsonb;
    _cmd_sql_fatal_error sql_errorish;
    _i int = 0;
    _iteration_start_time timestamptz;
    _queue_reselect_interval interval;
begin
    select q.* into _cmd_queue from cmd_queue as q where q.cmd_class = cmd_class$;

    _queue_reselect_interval := coalesce(queue_reselect_interval$, _cmd_queue.queue_reselect_interval);

    if _cmd_queue.queue_runner_role is not null then
        _old_role := current_user;
        execute format('SET LOCAL ROLE %I', _cmd_queue.queue_runner_role);
    end if;

    _old_timeout_ms := current_setting('statement_timeout')::int;
    _cmd_timeout_ms := coalesce(
        extract('epoch' from _cmd_queue.queue_cmd_timeout) * 1000
        ,0  -- “A value of zero (the default) turns this off.”
            -- And in `queue_cmd_timeout` this is expressed as `NULL`.
    );
    _select_timeout_ms := coalesce(
        extract('epoch' from _cmd_queue.queue_select_timeout) * 1000
        ,0  -- “A value of zero (the default) turns this off.”
            -- And in `queue_cmd_timeout` this is expressed as `NULL`.
    );

    <<main_loop>>
    loop
        _iteration_start_time := clock_timestamp();

        perform set_config('statement_timeout', _select_timeout_ms::text, true);
        execute format(
            'SELECT * FROM %s ORDER BY cmd_queued_since LIMIT 1 %s'
            ,_cmd_queue.cmd_class
            ,case when lock_rows_for_update$ then 'FOR UPDATE SKIP LOCKED' else '' end
        ) into _sql_queue_cmd;

        -- “Note in particular that EXECUTE changes the output of GET DIAGNOSTICS,
        -- but does not change FOUND.”
        get current diagnostics _sql_queue_cmd_count = row_count;
        exit main_loop when _sql_queue_cmd_count = 0 and iterate_until_empty$;

        _cmd_start_time := clock_timestamp();
        _cmd_sql_result_status := null;

        <<run_sql_cmd>>
        begin
            perform set_config('statement_timeout', _cmd_timeout_ms::text, true);
            raise debug using
                message = format('Executing %s', _cmd_queue.cmd_class)
                ,detail = jsonb_pretty(to_jsonb(_sql_queue_cmd));
            _cmd_sql_result_rows := null::jsonb;
            for _cmd_sql_result_row in execute _sql_queue_cmd.cmd_sql loop
                _cmd_sql_result_rows := coalesce(
                    _cmd_sql_result_rows || to_jsonb(array[to_jsonb(_cmd_sql_result_row)])
                    ,to_jsonb(array[to_jsonb(_cmd_sql_result_row)])
                );
            end loop;

            -- “Note in particular that EXECUTE changes the output of GET DIAGNOSTICS,
            -- but does not change FOUND.”
            get current diagnostics _cmd_sql_result_row_count := row_count;

            -- This detection is rather shit; a select which returns no rows should actually nevertheless
            -- cause a PGRES_TUPLES_OK status rather than PGRES_COMMAND_OK.
            if jsonb_array_length(_cmd_sql_result_rows) > 0 then
                _cmd_sql_result_status := 'PGRES_TUPLES_OK';
            else
                _cmd_sql_result_status := 'PGRES_COMMAND_OK';
            end if;
        exception
            when others then
                _cmd_sql_result_status := 'PGRES_FATAL_ERROR';
                get stacked diagnostics
                    _cmd_sql_fatal_error.pg_diag_sqlstate = returned_sqlstate
                    ,_cmd_sql_fatal_error.pg_diag_message_primary = message_text
                    ,_cmd_sql_fatal_error.pg_diag_message_detail = pg_exception_detail
                    ,_cmd_sql_fatal_error.pg_diag_message_hint = pg_exception_hint
                    ,_cmd_sql_fatal_error.pg_diag_schema_name = schema_name
                    ,_cmd_sql_fatal_error.pg_diag_table_name = table_name
                    ,_cmd_sql_fatal_error.pg_diag_column_name = column_name
                    ,_cmd_sql_fatal_error.pg_diag_datatype_name = pg_datatype_name
                    ,_cmd_sql_fatal_error.pg_diag_constraint_name = constraint_name
                    ,_cmd_sql_fatal_error.pg_diag_context = pg_exception_context
                ;
                _cmd_sql_fatal_error.pg_diag_severity := 'EXCEPTION';
                _cmd_sql_fatal_error.pg_diag_severity_nonlocalized := 'EXCEPTION';

                raise debug using
                    message = format('%s failed', _cmd_queue.cmd_class::regclass::text)
                    ,detail = jsonb_pretty(to_jsonb(_cmd_sql_fatal_error))::text;
        end run_sql_cmd;

        execute format(
            $sql$
UPDATE
    %s
SET
    cmd_runtime = tstzrange(%L, %L)
    ,cmd_sql_result_status = %L
    ,cmd_sql_fatal_error = %L
    ,cmd_sql_result_rows = %L
WHERE
    cmd_id = %L
    AND cmd_subid IS NOT DISTINCT FROM %s
$sql$
            ,_cmd_queue.cmd_class
            ,_cmd_start_time, clock_timestamp()
            ,_cmd_sql_result_status
            ,_cmd_sql_fatal_error
            ,_cmd_sql_result_rows
            ,_sql_queue_cmd.cmd_id
            ,coalesce(quote_literal(_sql_queue_cmd.cmd_subid), 'NULL')
        );

        -- Normally, `COMMIT AND CHAIN` would be what we want, but not while testing, because we may be
        -- within a PL/pgSQL exception handler (used to easily rollback a `pg_pure_tests` test case its
        -- state).
        if commit_between_iterations$ then
            commit and chain;
        end if;

        _i := _i + 1;
        if max_iterations$ is not null and _i = max_iterations$ then
            exit main_loop;
        end if;

        if _sql_queue_cmd_count = 0 and _queue_reselect_interval is not null then
            perform pg_sleep(
                extract('epoch' from _iteration_start_time + _queue_reselect_interval - clock_timestamp())
            );
        end if;
    end loop main_loop;

    if _cmd_queue.queue_runner_role is not null then
        execute format('SET LOCAL ROLE %I', _old_role);
    end if;

    perform set_config('statement_timeout', _old_timeout_ms::text, true);
end;
$$;

comment on procedure run_sql_cmd_queue is
$md$Run the commands from the given SQL command queue, mostly like the `pg_cmd_queue_daemon` would.

Unlike the `pg_cmd_queue_daemon`, this function cannot capture non-fatal errors
(like notices and warnings).  This is due to a limitation in PL/pgSQL.
$md$;

----------------------------------------------------------------------------------------------------------

/*
create function mock_run_nix_queue_cmd(inout nix_queue_cmd_template)
    language plpgsql
    as $$
begin
    if ($1).cmd_id is null then
        execute format(
            'SELECT c.* FROM %s'
            ,($1).cmd_class
        ) into $1;
    end if;
end;
$$;
*/

----------------------------------------------------------------------------------------------------------
