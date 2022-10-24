Build
-----
### `cmake && make && make install` Style Build
Build this library from source using [CMake][1]. Installation places a shared
library and public headers into the appropriate system directories
(`/usr/local/[...]`), or to a specified installation prefix.

A recent version of CMake is required (3.24), which might not be in your
system's package manager. [bin/install-cmake](bin/install-cmake) is an installer
for a recent CMake.

Here is how to install dd-trace-cpp into `.install/` within the source
repository.
```shell
$ git clone 'https://github.com/datadog/dd-trace-cpp'
$ cd dd-trace-cpp
$ bin/install-cmake
$ mkdir .install
$ mkdir .build
$ cd .build
$ cmake -DCMAKE_INSTALL_PREFIX=../.install ..
$ make -j $(nproc)
$ make install
$ find ../.install -type d
```

To instead install into `/usr/local/`, omit the `.install` directory and the
`-DCMAKE_INSTALL_PREFIX=../.install` option.

Then, when building an executable that uses `dd-trace-cpp`, specify the path to
the installed headers using an appropriate `-I` option.  If the library was
installed into the default system directories, then the `-I` option is not
needed.
```shell
$ c++ -I/path/to/dd-trace-cpp/.install/include -c -o my_app.o my_app.cpp
```

When linking an executable that uses `dd-trace-cpp`, specify linkage to the
built library using the `-ldd_trace_cpp` option and an appropriate `-L` option.
If the library was installed into the default system directories, then the `-L`
options is not needed. The `-ldd_trace_cpp` option is always needed.
```shell
$ c++ -o my_app my_app.o -L/path/to/dd-trace-cpp/.install/lib -ldd_trace_cpp
```
<!-- TODO: Do those commands need -pthread as well? -->

[1]: https://cmake.org/
