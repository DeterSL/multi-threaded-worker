#!/usr/bin/env bash
set -euo pipefail

export NATSURL="${NATSURL:-nats://127.0.0.1:4222}"

exec /usr/local/bin/detersl-worker "${DETERSL_THREADS:-6}"
