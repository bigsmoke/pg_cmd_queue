\o /dev/null
select  not :{?test_stage} as test_stage_missing
        ,not :{?extension_name} as extension_name_missing;
\o
\gset
\if :test_stage_missing
    \warn 'Missing `:test_stage` variable.'
    \quit
\endif
\if :extension_name_missing
    \warn 'Missing `:extension_name` variable.'
    \quit
\endif
\o /dev/null
select  :'test_stage' = 'pre-dump' as in_pre_dump_stage
        ,:'test_stage' = 'pre-restore' as in_pre_restore_stage;
\o
\gset

\set SHOW_CONTEXT 'errors'

\if :in_pre_restore_stage
    -- Let's generate some noise to offset the OIDs, to ensure that we're not relying on OIDs remaining the
    -- same between the moment of `pg_dump` and the moment of `pg_restore`.
    do $$
    declare
        _i int;
    begin
        for
            _i
        in select
            s.i
        from
            generate_series(1, 10) as s(i)
        loop
            execute format('CREATE TABLE test_dump_restore__oid_noise__tbl_%s (a int)', _i);
            execute format('CREATE TYPE test_dump_restore__oid_noise__rec_%s AS (a int, b int)', _i);
            execute format(
                'CREATE FUNCTION test_dump_restore__oid_noise__func_%s() RETURNS int RETURN 1'
                ,_i
            );
        end loop;
    end;
    $$;

    \quit
\endif

-- We put the `psql` variables into a temporary table, so that we can read them out from within the
-- PL/pgSQL`DO` block, as we cannot access these variables from within PL/pgSQL.
select
    :'extension_name' as extension_name
    ,:'test_stage' as test_stage
into temporary
    vars
;

do $$
declare
    _extension_name text := (select extension_name from vars);
    _test_stage text := (select test_stage from vars);
    _test_proc text;
begin
    if _test_stage = 'pre-dump' then
        execute format('CREATE EXTENSION %I WITH CASCADE', _extension_name);
    end if;

    for
        _test_proc
    in
    select
        case
            when pg_proc.prokind = 'p' then
                'CALL ' || pg_proc.oid::regproc::text
            else
                'PERFORM ' || pg_proc.oid::regproc::text
        end || '(' || quote_literal(_test_stage) || ')'
    from
        pg_depend
    inner join
        pg_proc
        on pg_proc.oid = pg_depend.objid
        and pg_depend.classid = 'pg_proc'::regclass
    where
        pg_depend.refclassid = 'pg_extension'::regclass
        and pg_depend.refobjid = (select oid from pg_extension where extname = _extension_name)
        and pg_proc.proname like 'test\_dump\_restore\_\_%'
        and pg_proc.prokind in ('f', 'p')
    loop
        raise notice '%', _test_proc;
        execute _test_proc;
    end loop;
end;
$$;
