#!/bin/bash

SCRIPT_DIR=$(cd "$(dirname "$(readlink "$0" || echo "$0")")" && pwd)
cd $SCRIPT_DIR

cmake .
cmake --build . --config Release

./gbench --benchmark_format=json > results.json
