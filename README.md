# C++ Secure HTTP/HTTPS Proxy


[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Linux](https://img.shields.io/badge/Platform-Linux%20%2F%20WSL-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://www.kernel.org/)
[![POSIX Sockets](https://img.shields.io/badge/Networking-POSIX%20Sockets-0078D4?style=for-the-badge)](https://man7.org/linux/man-pages/man2/socket.2.html)
[![CMake](https://img.shields.io/badge/Build-CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white)](https://cmake.org/)
[![C++ Threads](https://img.shields.io/badge/Concurrency-C%2B%2B%20Threads-2E8B57?style=for-the-badge)](https://en.cppreference.com/w/cpp/thread.html)
[![Git](https://img.shields.io/badge/Tools-Git-F05032?style=for-the-badge&logo=git&logoColor=white)](https://git-scm.com/)

A multithreaded HTTP forward proxy built in C++ using Linux POSIX sockets. The project explores network programming, concurrent client handling, domain-based access control, and HTTPS tunneling.

## Features

- **HTTP request forwarding:** Forwards supported HTTP GET requests to upstream servers and relays their responses.
- **HTTPS CONNECT tunneling:** Establishes TCP tunnels for HTTPS traffic without decrypting TLS payloads.
- **Domain blocklist:** Blocks configured domains and their subdomains with an HTTP `403 Forbidden` response.
- **Thread pool:** Uses four worker threads to handle client connections concurrently.
- **Bounded client queue:** Limits queued connections to 100 to avoid unbounded queue growth.
- **Buffered tunnel relay:** Uses `poll()` and non-blocking sockets to relay data in both directions, handling partial writes and buffered data.
- **Socket timeouts:** Applies receive timeouts to HTTP client and upstream sockets and an idle timeout to HTTPS tunnels.
- **Graceful shutdown:** Handles `SIGINT`, closes queued connections, wakes worker threads, and joins them.
- **Request logging:** Records timestamps, request destinations, and outcomes such as `ALLOWED`, `BLOCKED`, and `ERROR`.
- **Automated smoke tests:** Tests common success and error paths using shell scripts and `curl`.

## Tech Stack

- C++
- Linux POSIX sockets
- TCP/IP networking
- C++ threads, mutexes, condition variables, and queues
- `poll()` and non-blocking I/O
- CMake
- Bash and curl

## Architecture

1. The main thread creates a listening TCP socket on port `8080`.
2. Incoming client connections are placed in a bounded, thread-safe queue.
3. Worker threads retrieve connections from the queue and process client requests.
4. HTTP GET requests are checked against the domain blocklist before connecting to the upstream server.
5. HTTPS CONNECT requests are checked against the blocklist before a TCP connection to the destination is established.
6. For HTTPS, the proxy returns `200 Connection Established` and relays encrypted traffic in both directions until the tunnel closes or becomes idle.
7. Request outcomes are written to the console with timestamps.

## Requirements

- Linux or WSL with Ubuntu
- A C++17-compatible compiler
- CMake
- `make` or another supported CMake build tool
- `curl` for testing

## Build and Run

Clone the repository and enter its directory:

```bash
git clone https://github.com/Soupal23/cpp-secure-proxy.git
cd cpp-secure-proxy
```

Build the project:

```bash
cmake -S . -B build
cmake --build build
```

Start the proxy:

```bash
./build/cpp_secure_proxy
```

The proxy listens on port `8080`. Keep this terminal open while testing.

Stop it with `Ctrl+C`.

## Configuration

Blocked domains are loaded from:

```text
config/blocked_domains.txt
```

Add one domain per line, for example:

```text
example.com
blocked.test
```

The proxy also blocks subdomains of listed domains. For example, adding `example.com` blocks both `example.com` and `sub.example.com`.

Restart the proxy after changing the blocklist.

## Usage and Testing

In another terminal, test an allowed HTTP request:

```bash
curl -i -x http://127.0.0.1:8080 http://google.com/
```

Test a blocked HTTP request:

```bash
curl -i -x http://127.0.0.1:8080 http://example.com/
```

Test HTTPS tunneling:

```bash
curl -i -x http://127.0.0.1:8080 https://google.com/
```

Test a blocked HTTPS destination:

```bash
curl -i -x http://127.0.0.1:8080 https://example.com/
```

Run the automated smoke tests while the proxy is running:

```bash
bash tests/smoke_test.sh
```

The test suite covers allowed HTTP, blocked HTTP, allowed HTTPS, unsupported HTTP methods, blocked HTTPS CONNECT requests, and unreachable upstream destinations.

## Current Limitations

- Supports HTTP GET forwarding rather than a complete set of HTTP methods.
- Uses IPv4 address resolution for upstream connections.
- HTTPS traffic is tunneled without TLS inspection or decryption.
- Domain blocking is based on the requested hostname; this is not a complete enterprise-grade URL filtering or security system.
- The implementation is an educational project and has not undergone a comprehensive security audit or production hardening.
- The current thread-per-active-connection processing model can experience worker-pool starvation when many long-lived HTTPS tunnels remain open. An event-driven architecture using epoll is a planned improvement.

## Future Improvements

- More robust HTTP request parsing and header handling.
- Improved connection lifecycle management and error reporting.
- Per-client rate limiting.
- Response caching.
- Expanded automated tests and stress testing.

## Learning Objectives

This project provides practical experience with TCP sockets, HTTP request handling, concurrent programming, synchronization primitives, non-blocking I/O, buffered data transfer, and HTTPS CONNECT tunneling.
