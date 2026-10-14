# C++ Secure HTTP/HTTPS Proxy

[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Linux](https://img.shields.io/badge/Platform-Linux%20%2F%20WSL-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://www.kernel.org/)
[![POSIX Sockets](https://img.shields.io/badge/Networking-POSIX%20Sockets-0078D4?style=for-the-badge)](https://man7.org/linux/man-pages/man2/socket.2.html)
[![CMake](https://img.shields.io/badge/Build-CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white)](https://cmake.org/)
[![C++ Threads](https://img.shields.io/badge/Concurrency-C%2B%2B%20Threads-2E8B57?style=for-the-badge)](https://en.cppreference.com/w/cpp/thread.html)

A multithreaded HTTP/HTTPS forward proxy built in C++17 using Linux POSIX sockets. It supports HTTP GET/HEAD forwarding, domain-based access control, HTTPS CONNECT tunneling, bounded connection queuing, and concurrent client handling.

The project explores TCP socket programming, thread synchronization, non-blocking I/O, connection timeouts, and connection lifecycle management.

## Features

## Features

- **HTTP forwarding:** Forwards supported GET and HEAD requests and relays upstream responses.
- **HTTPS CONNECT tunneling:** Establishes TCP tunnels for HTTPS traffic without decrypting TLS payloads.
- **Domain blocklist:** Returns `403 Forbidden` for configured domains and their subdomains.
- **Hostname normalization:** Handles case differences and trailing dots during domain matching.
- **Worker pool:** Uses 64 worker threads to process client connections concurrently.
- **Bounded connection queue:** Limits pending connections to 100.
- **Buffered tunnel relay:** Uses `poll()` and non-blocking sockets to relay data in both directions, handling partial writes and buffered data.
- **Connection establishment timeout:** Uses non-blocking `connect()`, `poll()`, and `getsockopt(SO_ERROR)` to limit upstream connection establishment to approximately five seconds.
- **Socket timeouts:** Configures send and receive timeouts for applicable socket operations and an idle timeout for HTTPS tunnels.
- **Graceful shutdown:** Handles `SIGINT`, stops accepting new connections, closes queued connections, and joins worker threads. Shutdown checks in request-processing loops help workers respond to shutdown requests promptly.
- **Request logging:** Records timestamps, destinations, and outcomes such as `ALLOWED`, `BLOCKED`, and `ERROR`.
- **Automated smoke tests:** Uses Bash and `curl` to test common success and error paths.

## Tech Stack

- C++17
- Linux POSIX sockets and TCP/IP
- C++ threads, mutexes, condition variables, and queues
- `poll()`, `fcntl()`, and non-blocking I/O
- CMake
- Bash and `curl`

## Architecture

1. The main thread creates a listening TCP socket on port `8080`.
2. Incoming client connections are placed in a bounded, thread-safe queue.
3. Worker threads retrieve queued connections and process requests.
4. Supported HTTP requests are checked against the domain blocklist before the proxy connects to the upstream server.
5. HTTPS `CONNECT` requests are checked against the blocklist before an upstream TCP connection is established.
6. For an allowed HTTPS destination, the proxy returns `200 Connection Established` and relays encrypted traffic in both directions until the tunnel closes or becomes idle.
7. Request outcomes are logged to the console with timestamps.
8. On `SIGINT`, the proxy stops accepting new connections, cleans up queued connections, and joins its worker threads.

## Requirements

- Linux or WSL with Ubuntu
- A C++17-compatible compiler
- CMake
- GNU Make or another supported CMake build tool
- `curl`
- `netcat-openbsd` for optional manual tests

## Build and Run

Clone the repository:

```bash
git clone https://github.com/Soupal23/cpp-secure-proxy.git
cd cpp-secure-proxy
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

Start the proxy from the repository root:

```bash
./build/cpp_secure_proxy
```

The proxy listens on port `8080`. Keep this terminal open while testing.

Stop the proxy with `Ctrl+C`.

## Configuration

The blocklist is loaded from:

```text
config/blocked_domains.txt
```

Add one domain per line:

```text
example.com
blocked.test
```

A listed domain also blocks its subdomains. For example, `example.com` matches `example.com` and `sub.example.com`.

Restart the proxy after changing the blocklist.

## Usage and Testing

Run these commands in another terminal while the proxy is running.

**Allowed HTTP request**

```bash
curl -i -x http://127.0.0.1:8080 http://google.com/
```

**Blocked HTTP request**

```bash
curl -i -x http://127.0.0.1:8080 http://example.com/
```

**HTTPS tunnel**

```bash
curl -i -x http://127.0.0.1:8080 https://google.com/
```

**Blocked HTTPS destination**

```bash
curl -i -x http://127.0.0.1:8080 https://example.com/
```

**HEAD request**

```bash
curl -I -x http://127.0.0.1:8080 http://google.com/
```

**Run the smoke tests**

```bash
bash tests/smoke_test.sh
```

The smoke test suite checks:

1. Allowed HTTP request
2. Blocked HTTP request
3. Allowed HTTPS request
4. Unsupported HTTP method
5. Blocked HTTPS `CONNECT` request
6. Unreachable upstream destination

All six tests have passed in the development environment.

### Additional Manual Tests

Development testing also covered:

- Abrupt client disconnects and incomplete HTTP requests
- Invalid CONNECT ports and malformed request lines
- Case-insensitive hostname matching, trailing dots, and subdomain blocking
- Twenty concurrent incomplete clients while checking responsiveness to a legitimate request
- Shutdown with a silent client connected
- Shutdown while an HTTP response body was still being relayed

These are targeted development checks, not a comprehensive security audit or load test. Interrupting an in-progress download can result in a partial response, which is expected when the proxy closes the connection during shutdown.

## Benchmark

Measured with [wrk](https://github.com/wg/wrk) on a single machine (loopback), so the load
generator, a local upstream server, and the proxy share the same CPU cores.

- **Hardware:** 11th Gen Intel(R) Core(TM) i3-1115G4 @ 3.00GHz, 4 cores, WSL2 Ubuntu
- **Build:** Release (`-DCMAKE_BUILD_TYPE=Release`), proxy output redirected to `/dev/null`
- **Workload:** `GET` through the proxy to a local upstream; each request opens a new
  connection (`Connection: close`). 10 s runs, median of 3.

| Connections | Direct to upstream | Through proxy | p50 | p99 |
|---|---|---|---|---|
| 16 | 17,051 req/s | 4,985 req/s | 2.46 ms | 11.2 ms |
| 32 | 15,150 req/s | 5,388 req/s | 4.90 ms | 15.0 ms |
| 64 | 17,663 req/s | 5,403 req/s | 11.55 ms | 26.1 ms |

Throughput plateaus at about 5.4k req/s: every request involves an accept, a queue handoff
and a fresh upstream connection, so beyond ~32 connections extra clients only add queueing
delay (latency ≈ connections ÷ throughput). Next steps would be an `epoll`-based event loop
and upstream connection reuse.

Reproduce with `bench/upstream.py` and `bench/proxy.lua`.

## Current Limitations

- HTTP forwarding supports GET and HEAD rather than the complete set of HTTP methods.
- Upstream address resolution is configured for IPv4.
- HTTPS traffic is tunneled without TLS inspection or decryption.
- Domain blocking relies on the requested hostname; it is not a complete enterprise-grade URL filtering system.
- The implementation has not undergone a comprehensive security audit or production hardening.
- The fixed-size worker pool can become saturated when many long-lived connections occupy workers. An event-driven architecture using `epoll` is a possible future improvement.
- Request parsing and error handling are intentionally limited compared with a complete HTTP proxy implementation.

## Future Improvements

- More comprehensive HTTP parsing and header validation
- Expanded automated tests for malformed requests and connection failures
- Improved connection lifecycle management and observability
- Per-client connection limits and rate limiting
- An event-driven networking model using `epoll`
- Additional protocol and stress testing

## Learning Objectives

This project provided practical experience with TCP sockets, HTTP request handling, concurrent programming, synchronization primitives, non-blocking I/O, buffered data transfer, connection timeouts, and HTTPS CONNECT tunneling.