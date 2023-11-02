#!/bin/bash

: "${CMDQD_SIGUSR1_TIMEOUT:=2s}"
SCRIPT_NAME=$(basename "$0")

# `psql` will only return exit codes between 0 and 3; therefore any exit code > 3 will be distinguishable.
EXIT_CODE_ON_CMDQD_SIGUSR1_TIMEOUT=4
EXIT_CODE_ON_CMDQD_FAILURE=5

# Remember the argument array as a global variable, so that we can later access it from a function's scope.
ARGV=("$@")
# TODO: parse -d from $@

clean_exit() {
    local exit_code="${1:-0}"

    # Too bad that we can't just peak in `/proc/$cmdqd_pid` on MacOS.
    if [[ "$(basename "$(ps -p $cmdqd_pid -o comm=)" )" != "$(basename "$CMDQD_BIN")" ]]; then
        wait $cmdqd_pid  # The `pg_cmdqd` process is no longer running; so `wait` should return immediately.
        cmdqd_exit_code=$?
        if [[ $cmdqd_exit_code != 0 ]]; then
            echo -e "\x1b[31m\x1b[1m$(basename "$CMDQD_BIN")\x1b[22m failed with exit code \x1b[1m$cmdqd_exit_code\x1b[22m.\x1b[0m"
            exit_code=$EXIT_CODE_ON_CMDQD_FAILURE
        fi
    else  # `pg_cmdqd` is still running.
        kill "$cmdqd_pid"  # Send `SIGTERM` to `pg_cmdqd`.
        wait "$cmdqd_pid"
        cmdqd_exit_code=$?

        # `kill -0` just checks if the process is available to send a signal _to_; i.e., is it still running?
        while kill -0 "$cmdqd_pid" >/dev/null 2>&1; do
            sleep 0.1
        done
    fi

    if [ -n "$timer_pid" ]; then
        kill "$timer_pid"; timer_pid=
    fi

    exit $exit_code
    # From https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#exit :
    # > A trap on EXIT shall be executed before the shell terminates, except when the exit utility is invoked
    # > in that trap itself, in which case the shell shall exit immediately.
}

# `SIGUSR1` trap handler that launches the `psql` command constructed by our `pg_regress` parent process.
receive_sigusr1() {
    kill "$timer_pid"; timer_pid=

    # Ignore `SIGUSR2` rather than restore the default (termination) action in case the timer isn't dead yet
    # so soon after the kill.
    trap '' SIGUSR2

    # Run the `psql` command that `pg_regress` has us be the launcher to.
    "${ARGV[@]}"

    clean_exit 0
}

# The only way I could think of to make this script time out was to background a timer child process,
# and then either:
#   (a) let that child be killed when the previously spawned `pg_cmdqd` child process sends
#       us a `SIGUSR1`, or
#   (b) let the child emit a `SIGUSR2` signal back to the parent, which will than prompt us to
#       initiate a clean exit (_with_ error code, mind you).
send_sigusr2_after_timeout() {
    local main_pid=$1
    sleep "$CMDQD_SIGUSR1_TIMEOUT"
    kill -SIGUSR2 $main_pid
}

# `SIGUSR2` trap handler that forcefully kills `pg_cmdqd` and then commits seppuku when the daemon fails to
# report its readiness within the `$CMDQD_SIGUSR1_TIMEOUT`.
receive_sigusr2() {
    echo -e "\x1b[31m$CMDQD_SIGUSR1_TIMEOUT timeout expired while waiting to receive the \x1b[1mSIGUSR1\x1b[22m signal from \x1b[1m$(basename "$CMDQD_BIN")\x1b[22m.\x1b[0m"
    timer_pid=
    clean_exit $EXIT_CODE_ON_CMDQD_SIGUSR1_TIMEOUT
}

export PGDATABASE="contrib_regression"
"$CMDQD_BIN" --emit-sigusr1-when-ready --log-level LOG_DEBUG5 &
cmdqd_pid=$!

trap clean_exit EXIT
trap receive_sigusr1 SIGUSR1
trap receive_sigusr2 SIGUSR2
send_sigusr2_after_timeout $$ & timer_pid=$!

wait $cmdqd_pid
