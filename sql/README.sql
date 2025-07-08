\pset tuples_only
\pset format unaligned

begin;

create schema if not exists ext;
create extension if not exists hstore
    with schema ext;

create extension pg_cmd_queue
    cascade;

select cmdq.pg_cmd_queue_readme();

rollback;
