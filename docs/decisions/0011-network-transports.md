# 0011: MCP over HTTP is served by cpp-httplib, JSON-lines over TCP by sockets of our own

**Status:** accepted

## Context

[0005](0005-one-control-api.md) puts two transports on the desktop application: JSON-lines on
TCP and MCP over HTTP, both attached to the machine on screen. PGMEmu solved this in its record
0012, and its transports are taken unchanged.

## Decision

- **MCP over HTTP is served by [cpp-httplib](https://github.com/yhirose/cpp-httplib)**: MIT, a
  single header, fetched at a pinned tag. Only the header is used.
- **JSON-lines over TCP runs on sockets of our own** (`pet::server::TcpLineServer`): POSIX
  sockets, and Winsock on Windows.
- Both listen on 127.0.0.1 only. The HTTP server also turns away a browser's request whose
  `Origin` is not a local page.

## Consequences

- Serving beyond the loopback interface, or TLS, would need this revisited.
