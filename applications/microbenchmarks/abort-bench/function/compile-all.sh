#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")"

for file in write-string.py reserve-hotel.py reserve-flight.py create-order.py; do
    filename="${file%.*}"
    echo "Componentizing $filename ..."
    componentize-py \
        --wit-path ../../../hotel-reservation/wit \
        --world detersl-api \
        componentize "$filename" \
        -o "${filename}.wasm"
done

echo "Done."
