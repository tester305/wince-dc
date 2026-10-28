#!/usr/bin/env bash
set -e

if [ -z "$KOS_BASE" ]; then
    if [ -d "./kos" ]; then
        export KOS_BASE="$(pwd)/kos"
    elif [ -d "/opt/toolchains/dc/kos" ]; then
        export KOS_BASE="/opt/toolchains/dc/kos"
    elif [ -d "$HOME/kos" ]; then
        export KOS_BASE="$HOME/kos"
    else
        echo "Error: KOS_BASE is not set and 'kos' directory not found!" >&2
        exit 1
    fi
fi

if [ -f "$KOS_BASE/environ.sh" ]; then
    . "$KOS_BASE/environ.sh"
elif [ -f "$KOS_BASE/env/environ_base.sh" ]; then
    . "$KOS_BASE/env/environ_base.sh"
else
    echo "Error: Could not find environ.sh in $KOS_BASE" >&2
    exit 1
fi

make "$@"
