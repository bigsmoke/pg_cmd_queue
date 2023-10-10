#!/bin/bash

: "${CMDQD_SIGUSR1_TIMEOUT:=2s}"
SCRIPT_NAME=$(basename "$0")

# TODO: parse -d from $@
ARGV=("$@")

clean_exit() {
    local exit_code="${1:-0}"
    if [[ "$(ps -p $cmdqd_pid -o comm=)" != "$(basename "$CMDQD_BIN")" ]]; then
        wait $cmdqd_pid
        exit_code=$?
        if [[ $exit_code != 0 ]]; then
            echo -e "\x1b[31m\x1b[1m$(basename "$CMDQD_BIN")\x1b[22m failed with exit code \x1b[1m$exit_code\x1b[22m.\x1b[0m"
            exit 3
        fi
    else
        kill "$cmdqd_pid"

        # `kill -0` just checks if the process is available to send a signal _to_; i.e., is it still running?
        while kill -0 "$cmdqd_pid" >/dev/null 2>&1; do
            sleep 0.1
        done
    fi

    if [ -n "$timer_pid" ]; then
        kill "$timer_pid"; timer_pid=
    fi

    exit $exit_code
}

receive_sigusr1() {
    trap - SIGUSR2
    kill "$timer_pid"; timer_pid=
    "${ARGV[@]}"
}

# The only way I could think of to make this script time out was to background a timer child process,
# and then either:
#   (a) let the child be killed when the previously spawned `pg_cmdqd` child process sends
#       us a `SIGUSR1`, or
#   (b) let the child emit a `SIGUSR2` signal back to the parent, which will than prompt us to
#       initiate a clean exit (_with_ error code, mind you).
send_sigusr2_after_timeout() {
    local pid=$1
    sleep "$CMDQD_SIGUSR1_TIMEOUT"
    kill -SIGUSR2 $pid
}

receive_sigusr2() {
    echo -e "\x1b[31m$CMDQD_SIGUSR1_TIMEOUT timeout expired while waiting to receive the \x1b[1mSIGUSR1\x1b[22m signal from \x1b[1m$(basename "$CMDQD_BIN")\x1b[22m.\x1b[0m"
    timer_pid=
    clean_exit 4
}

export PGDATABASE="contrib_regression"
"$CMDQD_BIN" --emit-sigusr1-when-ready --log-level LOG_DEBUG5 &
cmdqd_pid=$!

trap clean_exit EXIT
trap receive_sigusr1 SIGUSR1
trap receive_sigusr2 SIGUSR2
send_sigusr2_after_timeout $$ & timer_pid=$!

wait $cmdqd_pid
