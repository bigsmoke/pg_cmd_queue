\pset tuples_only
\pset format unaligned

begin;

create extension pg_cmd_queue
    cascade;

select cmdq.pg_cmd_queue_readme();

rollback;
