# This is the image used to build and test the library in CircleCI.
# See .circleci/ for more information.

from ubuntu:22.04

# Don't issue blocking prompts during installation (sometimes an installer
# prompts for the current time zone).
env DEBIAN_FRONTEND=noninteractive

# Update the package lists and upgrade already-installed software.
run apt-get update && apt-get upgrade --yes

# Install build tooling:
# GCC, clang, make, coverage report generator, debugger, formatter, and miscellanea
run apt-get install --yes wget build-essential clang sed lcov gdb clang-format
# bazelisk, a launcher for bazel. `bazelisk --help` will cause the latest
# version to be downloaded.
run wget -O/usr/local/bin/bazelisk https://github.com/bazelbuild/bazelisk/releases/download/v1.15.0/bazelisk-linux-amd64 \
    && chmod +x /usr/local/bin/bazelisk \
    && bazelisk --help
# CMake, by downloading a recent release from their website.
copy bin/install-cmake /tmp/install-cmake
run chmod +x /tmp/install-cmake && /tmp/install-cmake && rm /tmp/install-cmake

# Coverage reporting and pushing to Github Pages.
run apt-get install --yes git ssh
copy bin/install-lcov /tmp/install-lcov
run chmod +x /tmp/install-lcov && /tmp/install-lcov && rm /tmp/install-lcov
