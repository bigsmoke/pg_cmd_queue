-- Complain if script is sourced in `psql`, rather than via `CREATE EXTENSION`.
\echo Use "CREATE EXTENSION pg_cmd_queue" to load this file. \quit

--------------------------------------------------------------------------------------------------------------

comment on extension pg_cmd_queue is
$markdown$
# The `pg_cmd_queue` PostgreSQL extension

## Setting up `pg_cmd_queue`

First, you need a PostgreSQL role with which the main thread of the
`pg_cmd_queue_runner` daemon can connect to the database.  This role needs to
be granted:

  * `USAGE` permission on the `cmd_queue` schema;
  * `SELECT` permission on the `cmd_queue` table;
  * membership of each `queue_runner_role` in the `cmd_queue` table.

## Planned features for `pg_cmd_queue`

* Built-in partitioning support for table-based queues, to easily get rid of
  table bloat.

## Features that will _not_ be part of `pg_cmd_queue`

* There will be no logging in `pg_cmd_queue`.  How to handle successes and
  failures is up to triggers on the `queue_cmd_class` tables or views which
  will be updated by the `pg_cmd_queue_runner` daemon after running a command.

* There will be no support for SQL callbacks, not even in `sql_queue_cmd`
  queues.  Again, this would be up to the implementor of a specific queue.
  If you want to use the generic `sql_queue_cmd` queue, just make sure that
  error handling logic is included in the `sql_cmd`.
$markdown$;

--------------------------------------------------------------------------------------------------------------

create type sql_error as (
    returned_sqlstate text
    ,column_name text
    ,constraint_name text
    ,pg_datatype_name text
    ,message_text text
    ,table_name text
    ,schema_name text
    ,pg_exception_detail text
    ,pg_exception_hint text
    ,pg_exception_context text
);

--------------------------------------------------------------------------------------------------------------

create function queue_callback(inout record)
    language plpgsql
    as $$
declare
    _cmd_queue record;
begin
    select
        *
    into
        _cmd_queue
    from
        cmd_queue
    where
        queue_cmd_class = ($1).queue_cmd_class
    ;

    assert (current_user = _cmd_queue.queue_callback_sql_role) is not false,
        format(
            'The `pg_cmd_queue_runner` should have `SET ROLE TO %I` before calling this callback.'
            ,_cmd_queue.queue_callback_sql_role
        );

    execute format(
        'UPDATE %s SET cmd_runtime = ($1).cmd_runtime WHERE cmd_id = ($1).cmd_id'
        ,_cmd_queue.queue_cmd_class
    )
        using $1
        into $1;
end;
$$;

--------------------------------------------------------------------------------------------------------------

create table cmd_queue (
    queue_id uuid
        primary key
        default uuid_generate_v7()
    ,cmd_queue_name text
        not null
        unique
        check (cmd_queue_name ~ '^[a-z][a-z0-9_]+_(nix|sql)_queue_cmd$')
    ,queue_signature_class regclass
        not null
        check (array_upper(parse_ident(queue_signature_class::text), 1) in ('nix_queue_cmd', 'sql_queue_cmd'))
    ,queue_cmd_class regclass
        not null
        unique
    ,check (array_upper(parse_ident(queue_cmd_class::text), 1) = cmd_queue_name)
    ,queue_runner_euid text
    ,queue_runner_egid text
    ,queue_runner_role name
    ,queue_notify_channel name
    ,queue_reselect_interval interval
        not null
        default '5 minutes'::interval
    ,queue_wait_time_limit_warn interval
    ,check ((queue_wait_time_limit_warn > queue_reselect_interval) is not false)
    ,queue_wait_time_limit_crit interval
    ,check ((queue_wait_time_limit_crit > queue_wait_time_limit_warn) is not false)
    ,check ((queue_wait_time_limit_crit > queue_reselect_interval) is not false)
    ,queue_created_at timestamptz
        not null
        default now()
    ,queue_callback_sql_role regrole
    ,queue_callback_sql_func regprocedure
        default 'queue_callback(record)'::regprocedure
    ,pg_extension_name text
);

comment on table cmd_queue is
$md$Every table or view which holds individual queue commands has to be registered as a record in this table.
$md$;

comment on column cmd_queue.queue_runner_role is
$md$This is the role as which the queue runner should select from the queue and run update commands.
$md$;

--------------------------------------------------------------------------------------------------------------
-- `queue_cmd_template` table template and related object                                                   --
--------------------------------------------------------------------------------------------------------------

create function queue_cmd_template__no_insert()
    returns trigger
    set schema to pg_catalog
    language plpgsql
    as $$
begin
    assert tg_when = 'BEFORE';
    assert tg_op = 'INSERT';
    assert tg_level = 'ROW';
    assert tg_table_schema = 'cmdq';
    assert tg_table_name ~ '_queue_cmd_template$';

    raise exception 'Don''t directly INSERT into this template table.';
end;
$$;

--------------------------------------------------------------------------------------------------------------

create table queue_cmd_template as (
    cmd_queue_name text
        not null
        references cmd_queue (cmd_queue_name)
            on delete cascade
            on update cascade
    ,cmd_id uuid
        primary key
        default uuid_generate_v7()
    ,cmd_queued_since timestamptz
        not null
        default now()
    ,cmd_runtime tstzrange
);

create trigger no_insert
    before insert
    on queue_cmd_template
    for each row
    execute function queue_cmd_template__no_insert();

--------------------------------------------------------------------------------------------------------------

create table nix_queue_cmd_template (
    like queue_cmd_template
       including all
    ,cmd_argv text[]
        not null
        check (array_length(cmd_argv, 1) >= 1)
    ,cmd_env hstore
        not null
    ,cmd_stdin bytea
    ,cmd_exit_code smallint
    ,cmd_stdout bytea
    ,cmd_stderr bytea
    ,constraint check_finished_in_full check (
        cmd_finished_at is null or (
            cmd_run_time is not null
            and cmd_exit_code is not null
            and cmd_stdout is not null
            and cmd_stderr is not null
        )
    )
);

create trigger no_insert
    before insert
    on nix_queue_cmd_template
    for each row
    execute function queue_cmd_template__no_insert();

--------------------------------------------------------------------------------------------------------------

create trigger no_insert
    before insert
    on queue_cmd_template
    for each row
    execute function queue_cmd_template__no_insert();

--------------------------------------------------------------------------------------------------------------

create table sql_queue_cmd_template (
    like queue_cmd_template
       including all
    ,cmd_sql text
        not null
);

--------------------------------------------------------------------------------------------------------------

insert into cmd_queue (
    queue_id
    ,queue_signature_class
    ,queue_cmd_class
    ,pg_extension_name
)
values (
    ,'01894ebb-ae85-793d-8bc1-23ff9249220d'
    ,'sql_queue_cmd'
    ,'sql_queue_cmd'
    ,'pg_cmd_queue'
);

--------------------------------------------------------------------------------------------------------------

create table nix_queue_cmd (
    like queue_cmd_template
       including all
    ,cmd_argv text[]
        not null
        check (array_length(cmd_argv, 1) >= 1)
    ,cmd_env hstore
        not null
    ,cmd_stdin bytea
    ,cmd_exit_code smallint
        check (cmd_exit_code between 0 and 255 is not false)
    ,cmd_stdout bytea
    ,cmd_stderr bytea
    ,constraint check_finished_in_full check (
        cmd_finished_at is null or (
            cmd_run_time is not null
            and cmd_exit_code is not null
            and cmd_stdout is not null
            and cmd_stderr is not null
        )
    )
);

comment on column queue_cmd.cmd_callback_sql is
$md$SQL statements to be `EXECUTE`d by a trigger on `queuer.queue_cmd` when the command finishes.

It will be `EXECUTE`d `USING NEW`. So you _must_ use `$1` somewhere in the statement.
$md$;

--------------------------------------------------------------------------------------------------------------

create function nix_queue_cmd__update()
    returns trigger
    set search_path from current
    language plpgsql
    as $$
declare
    _callback regprocedure;
begin
    assert tg_when = 'BEFORE';
    assert tg_op = 'UPDATE';
    assert tg_level = 'ROW';
    assert tg_nargs between 0 and 1;

    if tg_nargs > 0 then
        _callback := tg_argv[0];
    end if;

    if NEW.cmd_exit_code = 0 then
        if _callback is not null then

        end if;

        delete from nix_queue_cmd where cmd_id = OLD.cmd_id;
        return null;
    else
    end if;
end;
$$;

--------------------------------------------------------------------------------------------------------------

create function queue_callback(inout nix_queue_cmd)
    language plpgsql
    as $$
begin
    update
        nix_queue_cmd
    set
        cmd_finished_at = now()
    where
        cmd_id = ($1).cmd_id
    returning
        *
    into
        $1
    ;
end;
$$;

--------------------------------------------------------------------------------------------------------------

insert into cmd_queue (
    queue_id
    ,queue_signature_class
    ,queue_cmd_class
    ,queue_callback_sql_func
    ,pg_extension_name
)
values (
    ,'01894ebb-b1d9-7d09-8e8c-0c2c092c4b3a'
    ,'nix_queue_cmd'
    ,'nix_queue_cmd'
    ,'queue_callback(nix_queue_cmd)'::regprocedure
    ,'pg_cmd_queue'
);

--------------------------------------------------------------------------------------------------------------

create function queue_cmd__delete_after_update()
    returns trigger
    language plpgsql
    as $$
begin
    delete from
        queue_cmd
    where
        queue_cmd.cmd_id = OLD.cmd_id;

    return null;
end;
    $$;

create trigger 999_delete_after_update
    after update on queuer.queue_cmd
    for each row
    execute function queuer.queue_cmd__delete_after_update();

----------------------------------------------------------------------------------------------------------

create function queue_cmd__execute_callback_sql()
    returns trigger
    set search_path from current
    language plpgsql
    as $$
begin
    assert
    assert NEW.cmd_callback_sql is not null,
        'This trigger must be costrained by `WHEN (NEW.cmd_callback_sql IS NOT NULL)`.';

    execute NEW.cmd_callback_sql using NEW;
end;
$$;

--------------------------------------------------------------------------------------------------------------

create trigger 666_execute_callback_sql
    after udpate on queuer.queue_cmd
    for each row
    when (NEW.cmd_callback_sql is not null)
    execute function queuer.queue_cmd__execute_callback_sql();

----------------------------------------------------------------------------------------------------------

create view fmq_invoice_mail_nix_queue_cmd
with (security_barrier)
as
select
    'fmq_invoice_mail_nix_queue_cmd' as cmd_queue_name
    ,i.invoice_id as cmd_id
    ,i.inserted_at as cmd_queued_since
    ,null::tstzrange as cmd_runtime
    ,array['fmq_mail_invoice'] as cmd_argv
    ,null::hstore as cmd_env
    ,to_json(i.*)::text::bytea as cmd_stdin
    ,null as cmd_exit_code
    ,null as cmd_stdout
    ,null as cmd_stderr
from
    poen.invoice_with_lines as i
where
    i.invoice_emailed_at is null
;

----------------------------------------------------------------------------------------------------------
