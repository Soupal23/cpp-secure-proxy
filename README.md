# C++ Secure HTTP/HTTPS Proxy

[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Linux](https://img.shields.io/badge/Platform-Linux%20%2F%20WSL-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://www.kernel.org/)
[![POSIX Sockets](https://img.shields.io/badge/Networking-POSIX%20Sockets-0078D4?style=for-the-badge)](https://man7.org/linux/man-pages/man2/socket.2.html)
[![CMake](https://img.shields.io/badge/Build-CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white)](https://cmake.org/)
[![C++ Threads](https://img.shields.io/badge/Concurrency-C%2B%2B%20Threads-2E8B57?style=for-the-badge)](https://en.cppreference.com/w/cpp/thread.html)

A multithreaded HTTP/HTTPS forward proxy built in C++17 using Linux POSIX sockets. The project explores TCP networking, concurrent client handling, domain-based access control, non-blocking I/O, and HTTPS tunneling.

## Features

- **HTTP forwarding:** Forwards supported HTTP GET and HEAD requests to upstream servers and relays responses.
- **HTTPS CONNECT tunneling:** Establishes TCP tunnels for HTTPS traffic without decrypting TLS payloads.
- **Domain blocklist:** Blocks configured domains and their subdomains with an HTTP `403 Forbidden` response.
- **Hostname normalization:** Handles hostname capitalization and trailing dots during domain checks.
- **Worker pool:** Uses 64 worker threads to process client connections concurrently.
- **Bounded connection queue:** Limits pending connections to 100.
- **Buffered tunnel relay:** Uses `poll()` and non-blocking sockets to relay traffic in both directions, handling partial writes and buffered data.
- **Connection timeout:** Uses non-blocking upstream connection establishment with `poll()` and a 5-second connection-wait timeout.
- **Socket timeouts:** Configures receive and send timeouts for applicable blocking socket operations, plus an idle timeout for HTTPS tunnels.
- **Graceful shutdown:** Handles `SIGINT`, closes queued connections, wakes worker threads, and joins them.
- **Request logging:** Records timestamps, destinations, and outcomes such as `ALLOWED`, `BLOCKED`, and `ERROR`.
- **Automated smoke tests:** Tests common success and error paths using Bash and `curl`.

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

Additional manual tests covered abrupt client disconnects, incomplete requests, invalid CONNECT ports, hostname normalization, subdomain blocking, and 20 concurrent slow clients while a legitimate request remained responsive. These are manual development checks, not a comprehensive security or load test.

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