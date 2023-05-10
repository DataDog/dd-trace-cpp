from ubuntu:22.04 as build

# Install command line utilities and C++ build toolchain.
copy install-tooling /tmp/install-tooling
run /tmp/install-tooling

# Install the dd-trace-cpp library, as well as a version of CMake recent enough
# to build it.
copy install-dd-trace-cpp /tmp/install-dd-trace-cpp
run /tmp/install-dd-trace-cpp

# Build the server.
copy server.cpp httplib.h /tmp
# run c++ --std=c++17 -Wall -Wextra -pedantic -Og -o /tmp/server /tmp/server.cpp -l:libdd_trace_cpp.a
run c++ --std=c++17 -Wall -Wextra -pedantic -Og -o /tmp/server /tmp/server.cpp -ldd_trace_cpp

from ubuntu:22.04

# Copy the built server and its requisite libraries from the build image, and
# set the server as the command to run.
copy --from=build /usr/local/lib/libdd_trace_cpp.so /usr/local/lib/libdd_trace_cpp.so
copy --from=build /tmp/server /usr/local/bin/server
# Make the runtime linker aware of the installed libraries.
run ldconfig

cmd ["/usr/local/bin/server"]
