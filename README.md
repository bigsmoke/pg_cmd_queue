---
pg_extension_name: pg_cmd_queue
pg_extension_version: 0.1.0
pg_readme_generated_at: 2023-11-28 15:46:18.351053+00
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
2.  The command described in the tuple returned by the `SELECT` statement is
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
3.  Still within the same transaction, an `UPDATE` is performed on the record
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

### Schema: `cmdq`

`pg_cmd_queue` must be installed in the `cmdq` schema.  Hence, it is not relocatable.

### Tables

There are 5 tables that directly belong to the `pg_cmd_queue` extension.

#### Table: `cmd_queue`

Every table or view which holds individual queue commands has to be registered as a record in this table.

The `cmd_queue` table has 14 attributes:

1. `cmd_queue.cmd_class` `regclass`

   - `NOT NULL`
   - `CHECK ((parse_ident(cmd_class::text))[array_upper(parse_ident(cmd_class::text), 1)] ~ '^[a-z][a-z0-9_]+_cmd$'::text)`
   - `PRIMARY KEY (cmd_class)`

2. `cmd_queue.cmd_signature_class` `regclass`

   - `NOT NULL`
   - `CHECK ((parse_ident(cmd_signature_class::text))[array_upper(parse_ident(cmd_signature_class::text), 1)] = ANY (ARRAY['nix_queue_cmd_template'::text, 'sql_queue_cmd_template'::text]))`

3. `cmd_queue.queue_runner_role` `name`

   This is the role as which the queue runner should select from the queue and run update commands.

4. `cmd_queue.queue_notify_channel` `name`

5. `cmd_queue.queue_reselect_interval` `interval`

   - `NOT NULL`
   - `DEFAULT '00:05:00'::interval`

6. `cmd_queue.queue_reselect_randomized_every_nth` `integer`

   - `CHECK (queue_reselect_randomized_every_nth IS NULL OR queue_reselect_randomized_every_nth > 0)`

7. `cmd_queue.queue_select_timeout` `interval`

   - `DEFAULT '00:00:10'::interval`

8. `cmd_queue.queue_cmd_timeout` `interval`

9. `cmd_queue.queue_is_enabled` `boolean`

   - `NOT NULL`
   - `DEFAULT true`

10. `cmd_queue.queue_wait_time_limit_warn` `interval`

11. `cmd_queue.queue_wait_time_limit_crit` `interval`

12. `cmd_queue.queue_registered_at` `timestamp with time zone`

   - `NOT NULL`
   - `DEFAULT now()`

13. `cmd_queue.queue_metadata_updated_at` `timestamp with time zone`

   - `NOT NULL`
   - `DEFAULT now()`

14. `cmd_queue.pg_extension_name` `text`

#### Table: `queue_cmd_template`

The `queue_cmd_template` table has 5 attributes:

1. `queue_cmd_template.cmd_class` `regclass`

   - `NOT NULL`
   - `FOREIGN KEY (cmd_class) REFERENCES cmd_queue(cmd_class) ON UPDATE CASCADE ON DELETE CASCADE`

2. `queue_cmd_template.cmd_id` `text`

   Uniquely identifies an individual command in the queue (unless if `cmd_subid` is also required).

   When a single key in the underlying object of a queue command is sufficient to
   identify it, a `::text` representation of the key should go into this column.
   If multiple keys are needed—for example, when the underlying object has a
   multi-column primary key or when each underlying object can simultaneously
   appear in multiple commands the queue—you will want to use `cmd_subid` in
   addition to `cmd_id`.

   - `NOT NULL`

3. `queue_cmd_template.cmd_subid` `text`

   Helps `cmd_id` to uniquely identify commands in the queue, when just a `cmd_id` is not enough.

4. `queue_cmd_template.cmd_queued_since` `timestamp with time zone`

   - `NOT NULL`
   - `DEFAULT now()`

5. `queue_cmd_template.cmd_runtime` `tstzrange`

#### Table: `nix_queue_cmd_template`

The `nix_queue_cmd_template` table has 12 attributes:

1. `nix_queue_cmd_template.cmd_class` `regclass`

   - `NOT NULL`

2. `nix_queue_cmd_template.cmd_id` `text`

   Uniquely identifies an individual command in the queue (unless if `cmd_subid` is also required).

   When a single key in the underlying object of a queue command is sufficient to
   identify it, a `::text` representation of the key should go into this column.
   If multiple keys are needed—for example, when the underlying object has a
   multi-column primary key or when each underlying object can simultaneously
   appear in multiple commands the queue—you will want to use `cmd_subid` in
   addition to `cmd_id`.

   - `NOT NULL`

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
   - `DEFAULT ''::hstore`

8. `nix_queue_cmd_template.cmd_stdin` `bytea`

   - `NOT NULL`
   - `DEFAULT '\x'::bytea`

9. `nix_queue_cmd_template.cmd_exit_code` `integer`

10. `nix_queue_cmd_template.cmd_term_sig` `integer`

   If the command exited abnormally, this field should hold the signal with which it exited.

   In Unixy systems, a command exits either:
     (a) with an exit code, _or_
     (b) with a termination signal.

   Though not all *nix signals are standardized across different Unix variants,
   termination signals _are_ part of POSIX; see
   [Wikipedia](https://en.wikipedia.org/wiki/Signal_(IPC)#Default_action) and
   [GNU](https://www.gnu.org/software/libc/manual/html_node/Termination-Signals.html).

11. `nix_queue_cmd_template.cmd_stdout` `bytea`

12. `nix_queue_cmd_template.cmd_stderr` `bytea`

#### Table: `sql_queue_cmd_template`

The `sql_queue_cmd_template` table has 10 attributes:

1. `sql_queue_cmd_template.cmd_class` `regclass`

   - `NOT NULL`

2. `sql_queue_cmd_template.cmd_id` `text`

   Uniquely identifies an individual command in the queue (unless if `cmd_subid` is also required).

   When a single key in the underlying object of a queue command is sufficient to
   identify it, a `::text` representation of the key should go into this column.
   If multiple keys are needed—for example, when the underlying object has a
   multi-column primary key or when each underlying object can simultaneously
   appear in multiple commands the queue—you will want to use `cmd_subid` in
   addition to `cmd_id`.

   - `NOT NULL`

3. `sql_queue_cmd_template.cmd_subid` `text`

   Helps `cmd_id` to uniquely identify commands in the queue, when just a `cmd_id` is not enough.

4. `sql_queue_cmd_template.cmd_queued_since` `timestamp with time zone`

   - `NOT NULL`
   - `DEFAULT now()`

5. `sql_queue_cmd_template.cmd_runtime` `tstzrange`

6. `sql_queue_cmd_template.cmd_sql` `text`

   - `NOT NULL`

7. `sql_queue_cmd_template.cmd_sql_result_status` `sql_status_type`

8. `sql_queue_cmd_template.cmd_sql_fatal_error` `sql_errorish`

9. `sql_queue_cmd_template.cmd_sql_nonfatal_errors` `sql_errorish[]`

10. `sql_queue_cmd_template.cmd_sql_result_rows` `jsonb`

   The result rows represented as a JSON array of objects.

   When `cmd_sql` produced no rows, `cmd_sql` will contain either an SQL `NULL` or
   JSON `'null'` value.

#### Table: `http_queue_cmd_template`

The `http_queue_cmd_template` table has 12 attributes:

1. `http_queue_cmd_template.cmd_class` `regclass`

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

#### Procedure: `assert_queue_cmd_run_result (regclass, nix_queue_cmd_template, interval)`

Procedure arguments:

| Arg. # | Arg. mode  | Argument name                                                     | Argument type                                                        | Default expression  |
| ------ | ---------- | ----------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------- |
|   `$1` |       `IN` | `cmd_log_class$`                                                  | `regclass`                                                           |  |
|   `$2` |       `IN` | `expect$`                                                         | `nix_queue_cmd_template`                                             |  |
|   `$3` |       `IN` | `cmdqd_timeout$`                                                  | `interval`                                                           | `'00:00:10'::interval` |

#### Function: `cmd_class_color (regclass)`

Function arguments:

| Arg. # | Arg. mode  | Argument name                                                     | Argument type                                                        | Default expression  |
| ------ | ---------- | ----------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------- |
|   `$1` |       `IN` | ``                                                                | `regclass`                                                           |  |
|   `$2` |    `TABLE` | `r`                                                               | `integer`                                                            |  |
|   `$3` |    `TABLE` | `g`                                                               | `integer`                                                            |  |
|   `$4` |    `TABLE` | `b`                                                               | `integer`                                                            |  |
|   `$5` |    `TABLE` | `hex`                                                             | `text`                                                               |  |
|   `$6` |    `TABLE` | `ansi_fg`                                                         | `text`                                                               |  |
|   `$7` |    `TABLE` | `ansi_bg`                                                         | `text`                                                               |  |

Function return type: `TABLE(r integer, g integer, b integer, hex text, ansi_fg text, ansi_bg text)`

Function attributes: `IMMUTABLE`, `LEAKPROOF`, `PARALLEL SAFE`, ROWS 1000

#### Function: `cmd_line (nix_queue_cmd_template, boolean)`

Function arguments:

| Arg. # | Arg. mode  | Argument name                                                     | Argument type                                                        | Default expression  |
| ------ | ---------- | ----------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------- |
|   `$1` |       `IN` |                                                                   | `nix_queue_cmd_template`                                             |  |
|   `$2` |       `IN` |                                                                   | `boolean`                                                            | `false` |

Function return type: `text`

Function attributes: `IMMUTABLE`, `LEAKPROOF`, `PARALLEL SAFE`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`

#### Function: `cmd_line (text[], hstore, bytea)`

Function arguments:

| Arg. # | Arg. mode  | Argument name                                                     | Argument type                                                        | Default expression  |
| ------ | ---------- | ----------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------- |
|   `$1` |       `IN` | `cmd_argv$`                                                       | `text[]`                                                             |  |
|   `$2` |       `IN` | `cmd_env$`                                                        | `hstore`                                                             | `''::hstore` |
|   `$3` |       `IN` | `cmd_stdin$`                                                      | `bytea`                                                              | `'\x'::bytea` |

Function return type: `text`

Function attributes: `IMMUTABLE`, `LEAKPROOF`, `PARALLEL SAFE`

#### Function: `cmd_queue__create_queue_signature_downcast()`

Function return type: `trigger`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`

#### Function: `cmd_queue__notify_daemon_of_changes()`

Function return type: `trigger`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`

#### Function: `cmd_queue__queue_signature_constraint()`

Function return type: `trigger`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`

#### Function: `cmd_queue__update_updated_at()`

Function return type: `trigger`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`

#### Function: `nix_queue_cmd__require_exit_success()`

This trigger function will make a ruckus when a command finished with a non-zero exit code or with a termination signal.

`_exit_success()` refers to the `EXIT_SUCCESS` macro defined as an alias for
`0` in `stdlib.h`.

Function return type: `trigger`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`

#### Function: `nix_queue_cmd_template (record)`

Function arguments:

| Arg. # | Arg. mode  | Argument name                                                     | Argument type                                                        | Default expression  |
| ------ | ---------- | ----------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------- |
|   `$1` |       `IN` |                                                                   | `record`                                                             |  |

Function return type: `nix_queue_cmd_template`

Function attributes: `IMMUTABLE`, `LEAKPROOF`, `PARALLEL SAFE`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`

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

#### Function: `pg_cmd_queue_notify_channel()`

Function return type: `text`

Function attributes: `STABLE`, `LEAKPROOF`, `PARALLEL SAFE`

#### Function: `pg_cmd_queue_readme()`

This function utilizes the `pg_readme` extension to generate a thorough README for this extension, based on the `pg_catalog` and the `COMMENT` objects found therein.

Function return type: `text`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`
  *  `SET pg_readme.include_view_definitions TO true`
  *  `SET pg_readme.include_routine_definitions_like TO {test__%}`

#### Function: `pg_cmd_queue_search_path()`

Determing the `search_path` for within extension scripts similar to how `CREATE EXTENSION` would set it.

This allows us to easily set the `search_path` correctly when we are outside
the context of `CREATE EXTENSION`, for example:

1. when debugging a `.sql` script using `bin/debug-extension.sh`, or
2. while running testcases.

Function return type: `text`

Function attributes: `STABLE`, `LEAKPROOF`, `PARALLEL SAFE`

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

**Note** that this function is as of yet untested!

Function return type: `trigger`

#### Function: `queue_cmd__ignore_update()`

Function return type: `trigger`

Function-local settings:

  *  `SET search_path TO pg_catalog`

#### Function: `queue_cmd__insert_elsewhere()`

Function return type: `trigger`

#### Function: `queue_cmd__notify()`

Use this trigger function for easily triggering `NOTIFY` events from a queue's (underlying) table.

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

Function return type: `trigger`

Function-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`

#### Function: `queue_cmd_template__no_insert()`

Function return type: `trigger`

Function-local settings:

  *  `SET search_path TO pg_catalog`

#### Procedure: `run_sql_cmd_queue (regclass, bigint, boolean, interval, boolean, boolean)`

Run the commands from the given SQL command queue, mostly like the `pg_cmd_queue_daemon` would.

Unlike the `pg_cmd_queue_daemon`, this function cannot capture non-fatal errors
(like notices and warnings).  This is due to a limitation in PL/pgSQL.

Procedure arguments:

| Arg. # | Arg. mode  | Argument name                                                     | Argument type                                                        | Default expression  |
| ------ | ---------- | ----------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------- |
|   `$1` |       `IN` | `cmd_class$`                                                      | `regclass`                                                           |  |
|   `$2` |       `IN` | `max_iterations$`                                                 | `bigint`                                                             | `NULL::bigint` |
|   `$3` |       `IN` | `iterate_until_empty$`                                            | `boolean`                                                            | `true` |
|   `$4` |       `IN` | `queue_reselect_interval$`                                        | `interval`                                                           | `NULL::interval` |
|   `$5` |       `IN` | `commit_between_iterations$`                                      | `boolean`                                                            | `false` |
|   `$6` |       `IN` | `lock_rows_for_update$`                                           | `boolean`                                                            | `true` |

Procedure-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`

#### Function: `sql_queue_cmd__require_status_ok()`

Function return type: `trigger`

#### Procedure: `test__cmd_line()`

Procedure-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`
  *  `SET plpgsql.check_asserts TO true`

```sql
CREATE OR REPLACE PROCEDURE cmdq.test__cmd_line()
 LANGUAGE plpgsql
 SET search_path TO 'cmdq', 'public', 'pg_temp'
 SET "plpgsql.check_asserts" TO 'true'
AS $procedure$
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
$procedure$
```

#### Procedure: `test_dump_restore__pg_cmd_queue (text)`

Procedure arguments:

| Arg. # | Arg. mode  | Argument name                                                     | Argument type                                                        | Default expression  |
| ------ | ---------- | ----------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------- |
|   `$1` |       `IN` | `test_stage$`                                                     | `text`                                                               |  |

Procedure-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`
  *  `SET plpgsql.check_asserts TO true`

```sql
CREATE OR REPLACE PROCEDURE cmdq.test_dump_restore__pg_cmd_queue(IN "test_stage$" text)
 LANGUAGE plpgsql
 SET search_path TO 'cmdq', 'public', 'pg_temp'
 SET "plpgsql.check_asserts" TO 'true'
AS $procedure$
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
$procedure$
```

#### Procedure: `test_integration__pg_cmdqd (text)`

Procedure arguments:

| Arg. # | Arg. mode  | Argument name                                                     | Argument type                                                        | Default expression  |
| ------ | ---------- | ----------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------- |
|   `$1` |       `IN` | `test_stage$`                                                     | `text`                                                               |  |

#### Procedure: `test__pg_cmd_queue()`

Procedure-local settings:

  *  `SET search_path TO cmdq, public, pg_temp`
  *  `SET plpgsql.check_asserts TO true`

```sql
CREATE OR REPLACE PROCEDURE cmdq.test__pg_cmd_queue()
 LANGUAGE plpgsql
 SET search_path TO 'cmdq', 'public', 'pg_temp'
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
$procedure$
```

### Types

The following extra types have been defined _besides_ the implicit composite types of the [tables](#tables) and [views](#views) in this extension.

#### Enum type: `sql_status_type`

The possible SQL command result statuses.

[`PQresultStatus()`](https://www.postgresql.org/docs/current/libpq-exec.html#LIBPQ-PQRESULTSTATUS)

```sql
CREATE TYPE sql_status_type AS ENUM (
    'PGRES_EMPTY_QUERY',
    'PGRES_COMMAND_OK',
    'PGRES_TUPLES_OK',
    'PGRES_BAD_RESPONSE',
    'PGRES_FATAL_ERROR'
);
```

#### Composite type: `sql_errorish`

The field names of this type are the lowercased version of the field codes from
the documentation for libpq's
[`PQresultErrorField()`](https://www.postgresql.org/docs/current/libpq-exec.html#LIBPQ-PQRESULTERRORFIELD)
function.

```sql
CREATE TYPE sql_errorish AS (
  pg_diag_severity text,
  pg_diag_severity_nonlocalized text,
  pg_diag_sqlstate text,
  pg_diag_message_primary text,
  pg_diag_message_detail text,
  pg_diag_message_hint text,
  pg_diag_statement_position text,
  pg_diag_internal_position text,
  pg_diag_internal_query text,
  pg_diag_context text,
  pg_diag_schema_name text,
  pg_diag_table_name text,
  pg_diag_column_name text,
  pg_diag_datatype_name text,
  pg_diag_constraint_name text,
  pg_diag_source_file text,
  pg_diag_source_line text,
  pg_diag_source_function text
);
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
