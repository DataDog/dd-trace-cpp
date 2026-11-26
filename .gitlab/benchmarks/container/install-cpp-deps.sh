#!/usr/bin/env bash

set -e

apt-get update && apt-get install --yes build-essential git sed wget

git clone --depth 1 --branch main https://github.com/DataDog/dd-trace-cpp/ /dd-trace-cpp/
cd /dd-trace-cpp
chmod +x bin/install-cmake
bin/install-cmake
rm -rf /dd-trace-cpp
cd /
