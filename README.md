<img alt="datadog tracing nginx" src="mascot.svg" height="200"/>

Datadog Nginx Tracing Module
============================
This is the source for an nginx module that adds Datadog distributed tracing to
nginx.  The module is called `ngx_http_datadog_module`.

Status: Early Access
--------------------
This module is not yet considered "generally available" and is not yet
supported by Datadog customer support.

It is currently being piloted internally and will be expanded to wider use when
major version `1` is released.

Usage
-----
Download a gzipped tarball from a recent release, extract it to wherever nginx
looks for modules (e.g. `/usr/lib/nginx/modules/`) and add the following line
to the top of the main nginx configuration (e.g.  `/etc/nginx/nginx.conf`):
```nginx
load_module modules/ngx_http_datadog_module.so;
```
Tracing is automatically added to all endpoints by default.  For more
information, see [the API documentation](doc/API.md).

There is one version of the module for each [DockerHub nginx image tag][3]
supported by this project.  For example, a release artifact named
`1.19.1-alpine-ngx_http_datadog_module.so.tgz` contains an nginx module
`ngx_http_datadog_module.so` that is compatible with the nginx that is
distributed in the [nginx:1.19.1-alpine][5] DockerHub image.

Generally, nginx images tagged without a hyphen are built on Debian with
[glibc][6], while nginx images tagged with a trailing "`-alpine`" are built on
Alpine with [musl libc][7].

Build
-----
[Makefile](Makefile) is a [GNU make][1] compatible makefile.

Its default target, `build`, builds the Datadog nginx module and its
dependencies.  The resulting nginx module is
`.build/libngx_http_datadog_module.so`.

Another target, `build-in-docker`, builds the Datadog nginx module and its
dependencies in a [Docker][2] container compatible with the [DockerHub nginx
image tag][3] specified in a `./nginx-tag` file, e.g. `1.19.1-alpine`.  The
appropriate build image must be created first using the
[bin/docker-build.sh](bin/docker-build.sh) script if it does not exist already.
Once the image is built, `make build-in-docker` produces the nginx module as
`.docker-build/libngx_http_datadog_module.so`.

The C and C++ sources are built using [CMake][4].

The build does the following:

- Download a source release of nginx compatible with the DockerHub nginx image
  tag specified in `./nginx-tag`.
- Initialize the source trees of `opentracing-cpp` and `dd-opentracing-cpp` as
  git submodules.
- Install `dd-opentracing-cpp`'s dependencies (e.g. `libcurl`).
- Build `opentracing-cpp`, `dd-opentracing-cpp`, and the Datadog nginx module
  together using CMake.

Test
----
See [test/README.md](test/README.md).

Acknowledgements
----------------
This project is based largely on previous work.  See [CREDITS.md](CREDITS.md).

[1]: https://www.gnu.org/software/make/
[2]: https://www.docker.com/
[3]: https://hub.docker.com/_/nginx?tab=tags
[4]: https://cmake.org/
[5]: https://hub.docker.com/layers/nginx/library/nginx/1.19.1-alpine/images/sha256-966f134cf5ddeb12a56ede0f40fff754c0c0a749182295125f01a83957391d84
[6]: https://www.gnu.org/software/libc/
[7]: https://www.musl-libc.org/
