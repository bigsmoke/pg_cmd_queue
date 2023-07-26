---
pg_extension_name: pg_cmd_queue
pg_extension_version: 0.1.0
pg_readme_generated_at: 2023-07-26 17:17:09.282298+01
pg_readme_version: 0.6.4
---

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
often be the simplest.  Imagine, for example the scenario of a `website_user`
registration, for which a couple of mails need to be sent:

```sql
create schema wobbie;

create table wobbie.website_user (
    website_user_id uuid
        primary key
        default uuid_generate_v7()
    ,created_at timestamptz
        not null
        default now()
    ,email text
        not null
        unique
    ,email_verification_token uuid
        default gen_random_uuid()
    ,email_verified_at timestamptz
    ,password text
        not null
    ,password_reset_requested_at timestamptz
        not null
    ,password_reset_token uuid
        unique
);

create view cmdq.email_confirmation_mail_nix_queue_cmd
as
select
    'cmdq.email_confirmation_mail_nix_queue_cmd'::regclass
    ,u.website_user_id::text as cmd_id
    ,null::text as cmd_subid
    ,u.created_at as cmd_queued_since
    ,null as cmd_runtime
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
    ,null as cmd_exit_code
    ,null as cmd_stdout
    ,null as cmd_stderr
from
    wobbie.website_user as u
where
    u.email_verified_at is null
;

create function cmdq.email_confirmation_mail_nix_queue_cmd__instead_of_update()
    returns trigger()
    language plpgsql
    as $$
begin
    assert tg_when = 'BEFORE';
    assert tg_op = 'UPDATE';
    assert tg_level = 'ROW';
    assert tg_table_schema = 'cmdq';
    assert tg_table_name = 'email_confirmation_mail_nix_queue_cmd';

    if NEW.cmd_exit_code > 0 then
        raise exception format(
            E'%s returned a non-zero exit code (%s); stdout: %s\n\nstderr: %s'
            ,array_to_string(OLD.cmd_argv, ' ')
            ,NEW.cmd_exit_code
            ,NEW.cmd_stdout
            ,NEW.cmd_stderr
        );
    end if;

    update
        wobbie.website_user
    set
        email_verified_at = now()
    where
        website_user_id = cmd_id::uuid
    ;

    return NEW;
end;
$$;

create trigger instead_of_update
    instead of update
    on cmdq.email_confirmation_mail_nix_queue_cmd
    for each row
    execute function cmdq.email_confirmation_mail_nix_queue_cmd__instead_of_update();

insert into cmdq.cmd_queue (
    queue_cmd_class
    ,queue_signature_class
)
values (
    'cmdq.email_confirmation_mail_nix_queue_cmd'
    ,'nix_queue_cmd_template'
);
```

## Planned features for `pg_cmd_queue`

* Helpers for setting up partitioning for table-based queues, to easily get rid
  of table bloat.

## Features that will _not_ be part of `pg_cmd_queue`

* There will be no logging in `pg_cmd_queue`.  How to handle successes and
  failures is up to triggers on the `queue_cmd_class` tables or views which
  will be updated by the `pg_cmd_queue_runner` daemon after running a command.

* There will be no support for SQL callbacks, not even in `sql_queue_cmd`
  queues.  Again, this would be up to the implementor of a specific queue.
  If you want to use the generic `sql_queue_cmd` queue, just make sure that
  error handling logic is included in the `sql_cmd`.

## Extension object reference

### Schema: `cmdq`

`pg_cmd_queue` must be installed in the `cmdq` schema.  Hence, it is not relocatable.

### Tables

There are 5 tables that directly belong to the `pg_cmd_queue` extension.

#### Table: `cmd_queue`

Every table or view which holds individual queue commands has to be registered as a record in this table.

The `cmd_queue` table has 11 attributes:

1. `cmd_queue.queue_cmd_class` `regclass`

   - `NOT NULL`
   - `CHECK ((parse_ident(queue_cmd_class::text))[array_upper(parse_ident(queue_cmd_class::text), 1)] ~ '^[a-z][a-z0-9_]+_cmd$'::text)`
   - `PRIMARY KEY (queue_cmd_class)`

2. `cmd_queue.queue_signature_class` `regclass`

   - `NOT NULL`
   - `CHECK ((parse_ident(queue_signature_class::text))[array_upper(parse_ident(queue_signature_class::text), 1)] = ANY (ARRAY['nix_queue_cmd_template'::text, 'sql_queue_cmd_template'::text]))`

3. `cmd_queue.queue_runner_euid` `text`

4. `cmd_queue.queue_runner_egid` `text`

5. `cmd_queue.queue_runner_role` `name`

   This is the role as which the queue runner should select from the queue and run update commands.

6. `cmd_queue.queue_notify_channel` `name`

7. `cmd_queue.queue_reselect_interval` `interval`

   - `NOT NULL`
   - `DEFAULT '00:05:00'::interval`

8. `cmd_queue.queue_wait_time_limit_warn` `interval`

9. `cmd_queue.queue_wait_time_limit_crit` `interval`

10. `cmd_queue.queue_created_at` `timestamp with time zone`

   - `NOT NULL`
   - `DEFAULT now()`

11. `cmd_queue.pg_extension_name` `text`

#### Table: `queue_cmd_template`

The `queue_cmd_template` table has 5 attributes:

1. `queue_cmd_template.queue_cmd_class` `regclass`

   - `NOT NULL`
   - `FOREIGN KEY (queue_cmd_class) REFERENCES cmd_queue(queue_cmd_class) ON UPDATE CASCADE ON DELETE CASCADE`

2. `queue_cmd_template.cmd_id` `text`

   Uniquely identifies an individual command in the queue (unless if `cmd_subid` is also required).

   When a single key in the underlying object of a queue command is sufficient to
   identify it, a `::text` representation of the key should go into this column.
   If multiple keys are needed, for example, when the underlying object has a
   multi-column primary key or when each underlying object can simultaneously
   appear in multiple commands the queue, you will want to use `cmd_subid` in
   addition to `cmd_id`.

   - `NOT NULL`
   - `DEFAULT uuid_generate_v7()`

3. `queue_cmd_template.cmd_subid` `text`

   Helps `cmd_id` to uniquely identify commands in the queue, when just a `cmd_id` is not enough.

4. `queue_cmd_template.cmd_queued_since` `timestamp with time zone`

   - `NOT NULL`
   - `DEFAULT now()`

5. `queue_cmd_template.cmd_runtime` `tstzrange`

#### Table: `nix_queue_cmd_template`

The `nix_queue_cmd_template` table has 11 attributes:

1. `nix_queue_cmd_template.queue_cmd_class` `regclass`

   - `NOT NULL`

2. `nix_queue_cmd_template.cmd_id` `text`

   Uniquely identifies an individual command in the queue (unless if `cmd_subid` is also required).

   When a single key in the underlying object of a queue command is sufficient to
   identify it, a `::text` representation of the key should go into this column.
   If multiple keys are needed, for example, when the underlying object has a
   multi-column primary key or when each underlying object can simultaneously
   appear in multiple commands the queue, you will want to use `cmd_subid` in
   addition to `cmd_id`.

   - `NOT NULL`
   - `DEFAULT uuid_generate_v7()`

3. `nix_queue_cmd_template.cmd_subid` `text`

   Helps `cmd_id` to uniquely identify commands in the queue, when just a `cmd_id` is not enough.

4. `nix_queue_cmd_template.cmd_queued_since` `timestamp with time zone`

   - `NOT NULL`
   - `DEFAULT now()`

5. `nix_queue_cmd_template.cmd_runtime` `tstzrange`

6. `nix_queue_cmd_template.cmd_argv` `text[]`

   - `NOT NULL`
   - `CHECK (array_length(cmd_argv, 1) >= 1)`

7. `nix_queue_cmd_template.cmd_env` `hstore`

   - `NOT NULL`

8. `nix_queue_cmd_template.cmd_stdin` `bytea`

9. `nix_queue_cmd_template.cmd_exit_code` `smallint`

10. `nix_queue_cmd_template.cmd_stdout` `bytea`

11. `nix_queue_cmd_template.cmd_stderr` `bytea`

#### Table: `sql_queue_cmd_template`

The `sql_queue_cmd_template` table has 6 attributes:

1. `sql_queue_cmd_template.queue_cmd_class` `regclass`

   - `NOT NULL`

2. `sql_queue_cmd_template.cmd_id` `text`

   Uniquely identifies an individual command in the queue (unless if `cmd_subid` is also required).

   When a single key in the underlying object of a queue command is sufficient to
   identify it, a `::text` representation of the key should go into this column.
   If multiple keys are needed, for example, when the underlying object has a
   multi-column primary key or when each underlying object can simultaneously
   appear in multiple commands the queue, you will want to use `cmd_subid` in
   addition to `cmd_id`.

   - `NOT NULL`
   - `DEFAULT uuid_generate_v7()`

3. `sql_queue_cmd_template.cmd_subid` `text`

   Helps `cmd_id` to uniquely identify commands in the queue, when just a `cmd_id` is not enough.

4. `sql_queue_cmd_template.cmd_queued_since` `timestamp with time zone`

   - `NOT NULL`
   - `DEFAULT now()`

5. `sql_queue_cmd_template.cmd_runtime` `tstzrange`

6. `sql_queue_cmd_template.cmd_sql` `text`

   - `NOT NULL`

#### Table: `http_queue_cmd_template`

The `http_queue_cmd_template` table has 12 attributes:

1. `http_queue_cmd_template.queue_cmd_class` `regclass`

   - `NOT NULL`

2. `http_queue_cmd_template.cmd_id` `text`

   - `NOT NULL`

3. `http_queue_cmd_template.cmd_subid` `text`

4. `http_queue_cmd_template.cmd_queued_since` `timestamp with time zone`

   - `NOT NULL`

5. `http_queue_cmd_template.cmd_runtime` `tstzrange`

6. `http_queue_cmd_template.cmd_http_url` `text`

7. `http_queue_cmd_template.cmd_http_version` `text`

8. `http_queue_cmd_template.cmd_http_method` `text`

9. `http_queue_cmd_template.cmd_http_request_headers` `hstore`

10. `http_queue_cmd_template.cmd_http_request_body` `bytea`

11. `http_queue_cmd_template.cmd_http_response_headers` `hstore`

12. `http_queue_cmd_template.cmd_http_response_body` `bytea`

### Routines

#### Function: `pg_cmd_queue_meta_pgxn()`

Returns the JSON meta data that has to go into the `META.json` file needed for PGXN—PostgreSQL Extension Network—packages.

The `Makefile` includes a recipe to allow the developer to: `make META.json` to
refresh the meta file with the function's current output, including the
`default_version`.

`pg_cmd_queue` can be found on PGXN: https://pgxn.org/dist/pg_readme/

Function return type: `jsonb`

Function attributes: `STABLE`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`

#### Function: `pg_cmd_queue_readme()`

This function utilizes the `pg_readme` extension to generate a thorough README for this extension, based on the `pg_catalog` and the `COMMENT` objects found therein.

Function return type: `text`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`
  *  `SET pg_readme.include_view_definitions TO true`
  *  `SET pg_readme.include_routine_definitions_like TO {test__%}`

#### Function: `queue_cmd__delete_after_update()`

Use this trigger function if the queue holds records that need to deleted on `UPDATE`.

When using a table to hold a queue's commands, you can hook this trigger
function directly to an `ON INSERT` trigger on the `queue_cmd_template`-derived
table itself.  In that case, no arguments are needed.

When the `queue_cmd_template`-derived relation is a view, this trigger function
will have to be attached to the underlying table and will require two
arguments: ① the name of the field which will be mapped to `cmd_id` in the view;
and the name of the field which will be mapped to `cmd_subid` in the view.  If
there is no `cmd_subid`, this third parameter should be `null`.

Function return type: `trigger`

#### Function: `queue_cmd__notify()`

Use this trigger function for easily triggering `NOTIFY` events from a queue's (underlying) table.

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

Function return type: `trigger`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`

#### Function: `queue_cmd_template__no_insert()`

Function return type: `trigger`

Function-local settings:

  *  `SET search_path TO pg_catalog`

#### Procedure: `test__pg_cmd_queue()`

Procedure-local settings:

  *  `SET search_path TO cmdq, public, pg_temp,public`
  *  `SET plpgsql.check_asserts TO true`

```sql
CREATE OR REPLACE PROCEDURE cmdq.test__pg_cmd_queue()
 LANGUAGE plpgsql
 SET search_path TO 'cmdq', 'public', 'pg_temp', 'public'
 SET "plpgsql.check_asserts" TO 'true'
AS $procedure$
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
        ,null::smallint as cmd_exit_code
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
$procedure$
```

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

This `README.md` for the `pg_cmd_queue` extension was automatically generated using the [`pg_readme`](https://github.com/bigsmoke/pg_readme) PostgreSQL extension.
