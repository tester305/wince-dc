#!/usr/bin/env bash
set -e

# Automatically locate KOS_BASE if it's not already set in the environment
if [ -z "$KOS_BASE" ]; then
    if [ -d "/opt/toolchains/dc/kos" ]; then
        export KOS_BASE="/opt/toolchains/dc/kos"
    elif [ -d "$HOME/kos" ]; then
        export KOS_BASE="$HOME/kos"
    elif [ -d "./kos" ]; then
        export KOS_BASE="$(pwd)/kos"
    else
        echo "Error: KOS_BASE is not set and KallistiOS could not be found!" >&2
        exit 1
    fi
fi

# Source the KOS environment configuration safely
if [ -f "$KOS_BASE/environ.sh" ]; then
    source "$KOS_BASE/environ.sh"
elif [ -f "$KOS_BASE/env/environ_base.sh" ]; then
    source "$KOS_BASE/env/environ_base.sh"
else
    echo "Error: Could not find environ.sh in $KOS_BASE" >&2
    exit 1
fi

# Run make with any arguments passed to the script
make "$@"

