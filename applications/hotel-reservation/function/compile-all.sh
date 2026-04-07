#!/usr/bin/env bash

# Exit on error
set -euo pipefail

# Optional: target directory (default = current directory)
DIR="${1:-.}"

rm -rf /Users/alex/Library/Caches/BytecodeAlliance.wasmtime/*

# Enable nullglob so the loop skips if no .py files are found
shopt -s nullglob

for file in *.py; do
    # Get filename without directory
    filename="${file%.*}"
    
    
    echo "Componentizing $filename ..."
    
    componentize-py \
        --wit-path ../wit \
        --world detersl-api \
        componentize "$filename" \
        -o "${filename}.wasm"
done

echo "Done."
