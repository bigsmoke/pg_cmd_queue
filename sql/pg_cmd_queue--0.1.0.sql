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
how to connect.

`pg_cmd_queue` does _not_ come with any preconfigured queues.

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

When creating your own `_queue_cmd` tables or views, you may add additional
columns of your own, _after_ the columns that are specified by the
`_<cmd_type>_queue_cmd_template` that you choose to use.  Note that your custom
column names are _not_ allowed to start with `queue_` or `cmd_`.

## Running `pg_cmdqd` / `pg_command_queue_daemon`

*nix commands executed by `pg_cmdqd`, are passed the following environment
variables:

  * the `PATH` with which `pg_cmdqd` was executed.

## `pg_cmd_queue` settings

| Setting name                          | Default setting  |
| ------------------------------------- | ---------------- |
| `pg_cmd_queue.notify_channel`         | `pgcmdq`         |

## Planned features for `pg_cmd_queue`

* Helpers for setting up partitioning for table-based queues, to easily get rid
  of table bloat.
* `pg_cron`'s `cron` schema compatibility, so that you can use `pg_cron` without
  using `pg_cron`. 😉

## Features that will _not_ be part of `pg_cmd_queue`

* There will be no logging in `pg_cmd_queue`.  How to handle successes and
  failures is up to triggers on the `queue_cmd_class` tables or views which
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

create table cmd_queue (
    queue_cmd_class regclass
        primary key
        check (
            (parse_ident(queue_cmd_class::text))[
                array_upper(parse_ident(queue_cmd_class::text), 1)
            ] ~ '^[a-z][a-z0-9_]+_cmd$'
        )
    ,queue_signature_class regclass
        not null
        check (
            (parse_ident(queue_signature_class::text))[
                array_upper(parse_ident(queue_signature_class::text), 1)
            ] in ('nix_queue_cmd_template', 'sql_queue_cmd_template')
        )
    /*
    ,queue_runner_euid text
    ,queue_runner_egid text
    */
    ,queue_runner_role name
    ,queue_notify_channel name
    ,queue_reselect_interval interval
        not null
        default '5 minutes'::interval
    ,queue_reselect_randomized_every_nth int
        check (queue_reselect_randomized_every_nth is null or queue_reselect_randomized_every_nth > 0)
    ,queue_cmd_timeout interval
    ,queue_wait_time_limit_warn interval
    ,queue_wait_time_limit_crit interval
    ,queue_created_at timestamptz
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

-- TODO: Check for illicit `queue_` or `cmd_`columns.
create function cmd_queue__queue_signature_constraint()
    returns trigger
    set search_path from current
    language plpgsql
    as $$
declare
    _queue_rel_attrs text[];
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
        attrelid = NEW.queue_signature_class
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
        attrelid = NEW.queue_cmd_class
        and attnum > 1
    ;

    if _queue_rel_attrs[1:_signature_attr_count] != _signature_attrs then
        raise integrity_constraint_violation using
            message = format(
                'The first %s columns of the queue table `%s` do not match the `%s` signature table.'
                ,_signature_attr_count
                ,NEW.queue_cmd_class
                ,NEW.queue_signature_class
            )
            ,detail = format('%s ≠ %s', _queue_rel_attrs[1:_signature_attr_count], _signature_attrs)
            ,hint = format(
                'You can derive your queue table from `%s` or `%s`.'
                ,'nix_queue_cmd_template'::regclass
                ,'sql_queue_cmd_template'::regclass
            );
    end if;

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
    _channel text := coalesce(nullif(current_setting('pg_cmd_queue.notify_channel', true), ''), 'pgcmdq');
begin
    assert tg_when = 'AFTER';
    assert tg_op in ('INSERT', 'UPDATE', 'DELETE');
    assert tg_level = 'ROW';
    assert tg_table_schema = 'cmdq';
    assert tg_table_name = 'cmd_queue';

    if tg_op = 'INSERT' then
        perform pg_notify(_channel, hstore('inserted queue', NEW.queue_cmd_class::text)::text);
    elsif tg_op = 'UPDATE' then
        perform pg_notify(_channel, hstore('updated queue', NEW.queue_cmd_class::text)::text);
    elsif tg_op = 'DELETE' then
        perform pg_notify(_channel, hstore('deleted queue', NEW.queue_cmd_class::text)::text);
    end if;

    return null;
end;
$$;

create trigger notify_daemon_of_changes
    after insert or update or delete
    on cmd_queue
    for each row
    execute function cmd_queue__notify_daemon_of_changes();

--------------------------------------------------------------------------------------------------------------

create function queue_cmd_class_color(regclass)
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
            md5(fully_qualified_name) as queue_cmd_class_hash
        from
            fqn
    )
    ,rgb as (
        select
            ('x' || substring(md5(queue_cmd_class_hash), 1, 2))::bit(8)::int as r
            ,('x' || substring(md5(queue_cmd_class_hash), 3, 2))::bit(8)::int as g
            ,('x' || substring(md5(queue_cmd_class_hash), 5, 2))::bit(8)::int as b
        from
            hash
    )
    ,brighter as (
        select
            ((256 - 100) * (rgb.r/256.0) + 100)::int as r
            ,((256 - 100) * (rgb.g/256.0) + 100)::int as g
            ,((256 - 100) * (rgb.b/256.0) + 100)::int as b
        from
            rgb
    )
    select
        rgb.*
        ,to_hex(rgb.r) || to_hex(rgb.g) || to_hex(rgb.b) as hex
        ,format(E'[38;2;%s;%s;%sm', rgb.r, rgb.g, rgb.b) as ansi_fg
        ,format(E'[48;2;%s;%s;%sm', rgb.r, rgb.g, rgb.b) as ansi_bg
    from
        brighter as rgb
    ;
end;

--------------------------------------------------------------------------------------------------------------
-- `queue_cmd_template` table template and related objects                                                  --
--------------------------------------------------------------------------------------------------------------

create table queue_cmd_template (
    queue_cmd_class regclass
        not null
        references cmd_queue (queue_cmd_class)
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
If multiple keys are needed, for example, when the underlying object has a
multi-column primary key or when each underlying object can simultaneously
appear in multiple commands the queue, you will want to use `cmd_subid` in
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
    _queue_notify_channel name;
    _cmd_id_field name := 'cmd_id';
    _cmd_subid_field name := 'cmd_subid';
    _cmd_id text;
    _cmd_subid text;
begin
    assert tg_when = 'AFTER';
    assert tg_op in ('INSERT', 'UPDATE', 'DELETE');
    assert tg_level = 'ROW';
    assert tg_nargs in (1, 3);

    _queue_notify_channel := tg_argv[0];
    if tg_nargs = 3 then
        _cmd_id_field := tg_argv[1];
        _cmd_subid_field := nullif(tg_argv[2], 'null');
    end if;

    if tg_op in ('INSERT', 'UPDATE') then
        execute format('SELECT (($1).%I)::text', _cmd_id_field) using NEW into _cmd_id;
        if _cmd_subid_field is not null then
            execute format('SELECT (($1).%I)::text', _cmd_subid_field) using NEW into _cmd_subid;
        end if;
    elsif tg_op = 'DELETE' then
        execute format('SELECT (($1).%I)::text', _cmd_id_field) using OLD into _cmd_id;
        if _cmd_subid_field is not null then
            execute format('SELECT (($1).%I)::text', _cmd_subid_field) using OLD into _cmd_subid;
        end if;
    end if;

    perform pg_notify(_queue_notify_channel, row(_cmd_id, _cmd_subid)::text);

    return null;
end;
$$;

comment on function queue_cmd__notify() is
$md$Use this trigger function for easily triggering `NOTIFY` events from a queue's (underlying) table.

When using a table to hold a queue's commands, you can hook this trigger
function directly to an `ON INSERT` trigger on the `queue_cmd_template`-derived
table itself.  In that case, the only argument that this trigger function needs
is the `NOTIFY` channel name (which should be identical to the channel name in
the `cmd_queue.queue_notify_channel` column.

When the `queue_cmd_template`-derived relation is a view, this trigger function
will have to be attached to the underlying table and will require two
additional arguments: ① the name of the field which will be mapped to `cmd_id`
in the view; and the name of the field which will be mapped to
`cmd_subid` in the view.  If there is no `cmd_subid`, this third parameter
should be `null`.
$md$;

--------------------------------------------------------------------------------------------------------------

-- FIXME: BEFORE or AFTER?
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
        ,tg_relid
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

-- TODO: Add _v1 suffix
create table nix_queue_cmd_template (
    like queue_cmd_template
       including all
    ,cmd_argv text[]
        not null
        check (array_length(cmd_argv, 1) >= 1)
    ,cmd_env hstore
        not null
    ,cmd_stdin bytea
    ,cmd_exit_code int
    ,cmd_term_sig int
    ,cmd_stdout bytea
    ,cmd_stderr bytea
    ,constraint check_exited_normally_or_not check (
        num_nulls(cmd_exit_code, cmd_term_sig) in (0, 1)
    )
    ,constraint check_finished_in_full check (
        num_nulls(
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

-- TODO: Add _v1 suffix
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
            queue_cmd_class
            ,queue_signature_class
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
        queue_cmd_class
        ,queue_signature_class
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
            queue_cmd_class
            ,queue_signature_class
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

----------------------------------------------------------------------------------------------------------

create procedure test_integration__pg_cmdqd(test_stage$ text)
    set search_path from current
    set plpgsql.check_asserts to true
    language plpgsql
    as $$
declare
    _cmd record;
    _cmd_id text;
    _wait_start timestamptz;
begin
    assert test_stage$ in ('init', 'run', 'clean');

    if test_stage$ = 'init' then
        create table tst1_nix_cmd (
            like nix_queue_cmd_template
                including all
        );
        alter table tst1_nix_cmd
            alter column queue_cmd_class set default 'tst1_nix_cmd';

        insert into cmd_queue (
            queue_cmd_class
            ,queue_signature_class
            ,queue_runner_role
            ,queue_notify_channel
            ,queue_reselect_interval
            ,queue_cmd_timeout
        )
        values (
            'tst1_nix_cmd'
            ,'nix_queue_cmd_template'
            ,'cmdq_test_role'
            ,'tst1_nix_cmd'
            ,'0 seconds'::interval  -- Let the daemon's epoll() loop go without pause
            ,'1 second'::interval
        );

    elsif test_stage$ = 'run' then
        insert into tst1_nix_cmd (
            cmd_id
            ,cmd_argv
            ,cmd_env
            ,cmd_stdin
        )
        values (
            'cmd-with-clean-exit'
            ,array[
                'pg_cmdq_test_cmd'
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
            ,'PG_CMDQ_TST_VAR1=>var1_value",PG_CMDQ_TST_VAR2=>"var2: with => signs, \\ and \""'::hstore
            ,E'This STDIN should be echoed to STDOUT,\nincluding this 2nd line.'::bytea
        )
        returning cmd_id into _cmd_id
        ;

        _wait_start := clock_timestamp();
        <<wait>>
        loop
            select * into _cmd from tst1_nix_cmd where cmd_id = _cmd_id and cmd_runtime is not null;
            exit when found;
            if clock_timestamp() - _wait_start() > '10 seconds'::interval then
                raise assert_failure using message = format('Waited > 10s for pg_cmdqd to run %s', _cmd_id);
            end if;
            pg_sleep(0.001);  -- seconds
        end loop;

        assert _cmd.cmd_stdout = $out$This line should be sent to STDOUT.
This STDIN should be echoed to STDOUT,
including this 2nd line.
This line should be printed after the echoed STDIN.
var1_value
This line should be squeezed between 2 env. variables.
var2: with => signs, \ and "
$out$;
        assert _cmd.cmd_stderr = E'This line is to be sent to STDERR.\n';
        assert _cmd.cmd_exit_code = 0;
        assert _cmd.cmd_term_sig is null;

        insert into tst1_nix_cmd (
            cmd_id
            ,cmd_argv
            ,cmd_env
            ,cmd_stdin
        )
        values (
            'cmd-exceeds-timeout'
            ,array[
                'pg_cmdq_test_cmd'
                ,'--stdout-line'
                ,'Line 1.'
                ,'--sleep'
                ,'10 seconds'::interval
                ,'--exit-code'
                ,'0'
            ]
            ,'PG_CMDQ_TST_VAR1=>var1_value,PG_CMDQ_TST_VAR2=>var2_value'::hstore
            ,E'This STDIN should be echoed to STDOUT,\nincluding this 2nd line.'::bytea
        )
        returning cmd_id into _cmd_id
        ;

        _wait_start := clock_timestamp();
        <<wait>>
        loop
            select * into _cmd from tst1_nix_cmd where cmd_id = _cmd_id and cmd_runtime is not null;
            exit when found;
            if clock_timestamp() - _wait_start() > '10 seconds'::interval then
                raise assert_failure using message = format('Waited > 10s for pg_cmdqd to run %s', _cmd_id);
            end if;
            pg_sleep(0.001);  -- seconds
        end loop;

        assert _cmd.cmd_stdout = E'Line 1.\n';
        assert _cmd.cmd_stderr = '';
        assert _cmd.cmd_exit_code is null;
        assert _cmd.cmd_term_sig = 9, format('%s ≠ 9 (SIGKILL)', _cmd.cmd_term_sig);

    elsif test_stage$ = 'clean' then
        drop table tst1_nix_cmd cascade;
        delete from cmd_queue where queue_cmd_class = 'tst1_nix_cmd';
    end if;
end;
$$;

----------------------------------------------------------------------------------------------------------

create procedure run_sql_cmd_queue(
        queue_cmd_class$ regclass
        ,max_iterations$ bigint default null
        ,iterate_until_empty$ bool default true
        ,queue_reselect_interval$ interval default null
        ,commit_between_iterations$ bool default false
    )
    set search_path from current
    language plpgsql
    as $$
declare
    _old_role name;
    _old_timeout_ms int;
    _cmd_timeout_ms int;
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
    select q.* into _cmd_queue from cmd_queue as q where q.queue_cmd_class = queue_cmd_class$;

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
    if _old_timeout_ms != _cmd_timeout_ms then
        perform set_config(_cmd_timeout_ms, true);
    end if;

    <<main_loop>>
    loop
        _iteration_start_time := clock_timestamp();

        execute format(
            'SELECT * FROM %s ORDER BY cmd_queued_since LIMIT 1 FOR UPDATE'
            ,_cmd_queue.queue_cmd_class
        ) into _sql_queue_cmd;

        -- “Note in particular that EXECUTE changes the output of GET DIAGNOSTICS,
        -- but does not change FOUND.”
        get current diagnostics _sql_queue_cmd_count = row_count;
        if _sql_queue_cmd_count = 0 and iterate_until_empty$ then
            exit main_loop;
        end if;

        _cmd_start_time := clock_timestamp();
        _cmd_sql_result_status := null;

        <<run_sql_cmd>>
        begin
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
            ,_cmd_queue.queue_cmd_class
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

    if _old_timeout_ms != _cmd_timeout_ms then
        perform set_config(_old_timeout_ms, true);
    end if;
end;
$$;

comment on procedure run_sql_cmd_queue is
$md$Run the commands from the given SQL command queue, mostly like the `pg_cmd_queue_daemon` would.

Unlike the `pg_cmd_queue_daemon`, this function cannot capture non-fatal errors
(like notices and warnings).  This is due to a limitation in PL/pgSQL.
$md$;

----------------------------------------------------------------------------------------------------------
