\pset tuples_only
\pset format unaligned

begin;

create extension pg_cmd_queue
    cascade;

select jsonb_pretty(cmdq.pg_cmd_queue_meta_pgxn());

rollback;
