Example Usage
=============
This directory contains two example projects that illustrate how dd-trace-cpp
can be used to add Datadog tracing to a C++ application.

- [hasher](hasher) is a command-line tool that creates a complete trace
  involving only one service.
- [http-server](http-server) is an ensemble of services, including one C++
  service traced using this library. The traces generated are distributed
  across all of the services in the example.
