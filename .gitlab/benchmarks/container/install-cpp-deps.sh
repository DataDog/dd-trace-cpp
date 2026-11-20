#!/usr/bin/env bash

set -e

apt-get update && apt-get install --yes software-properties-common && \
  add-apt-repository ppa:git-core/ppa --yes && \
  apt-get update && apt-get upgrade --yes && \
  apt-get install --yes \
      wget build-essential clang sed gdb clang-format git ssh shellcheck \
      libc++-dev libc++abi-dev

git clone --branch main https://github.com/DataDog/dd-trace-cpp/ /dd-trace-cpp/ && cd /dd-trace-cpp/
cp bin/install-cmake /tmp/install-cmake
chmod +x /tmp/install-cmake && /tmp/install-cmake && rm /tmp/install-cmake
cd / && rm -rf /dd-trace-cpp

git clone https://github.com/google/benchmark.git
cd benchmark
cmake -E make_directory "build"
cmake -E chdir "build" cmake -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on -DCMAKE_BUILD_TYPE=Release ../
cmake --build "build" --config Release
cmake -E chdir "build" ctest --build-config Release
cmake --build "build" --config Release --target install
