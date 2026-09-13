# Internet Radio Client
[![CI](https://github.com/hanna-kaliszuk/Internet-Radio-Client/actions/workflows/tests.yml/badge.svg)](https://github.com/hanna-kaliszuk/Internet-Radio-Client/actions/workflows/tests.yml)

**A C++20 network client built from the socket layer up.**

Internet Radio Client is a command-line application for receiving and playing Internet radio streams over **TCP/TLS**, with support for **IPv4 and IPv6, HTTP/HTTPS, redirects, cookies, ICY metadata, configurable timeouts, and automatic reconnection**.

The project focuses on low-level network programming and protocol handling using the **POSIX sockets API**.

**C++20 · POSIX Sockets · TCP/IP · IPv4/IPv6 · HTTP · TLS · OpenSSL · Multithreading · `poll()` · Automated Testing · GitHub Actions**

---

## Why this project?

The client implements the complete path from a radio stream URL to a continuous audio stream:

```text
URL
 │
 ▼
URL parsing
 │
 ▼
DNS / getaddrinfo()
 │
 ▼
IPv4 / IPv6
 │
 ▼
TCP connection
 │
 ├───────────────┐
 ▼               ▼
TCP            TLS / OpenSSL
 └───────────────┘
         │
         ▼
    HTTP request
         │
         ▼
  HTTP response
         │
    ┌────┴─────┐
    │          │
   200       3xx
    │          │
    │      redirect +
    │        cookies
    │          │
    └────┬─────┘
         ▼
    Audio stream
         │
         ▼
   ICY demultiplexing
      │          │
      ▼          ▼
    audio     metadata
    stdout     stderr
```

The implementation therefore covers multiple layers of network communication instead of delegating the entire problem to a high-level HTTP client.

---

## Key Features

### Low-level networking

- Direct communication through **POSIX sockets**
- DNS and address resolution with `getaddrinfo()`
- **IPv4 and IPv6** support
- Automatic selection between available addresses
- Explicit IPv4/IPv6 selection
- TCP connection management
- Configurable socket receive timeouts

### TCP and TLS

The transport layer is abstracted behind a common `IStream` interface:

```text
             IStream
             /     \
            /       \
     TcpStream    TlsStream
        │             │
       TCP       TLS / OpenSSL
```

Higher-level HTTP and streaming logic does not need to distinguish between the two transport implementations.

### HTTP / HTTPS

The client implements its own HTTP request and response handling, including:

- HTTP status codes
- response headers
- HTTP redirects
- `Location`
- `Set-Cookie`
- cookie propagation between redirected requests
- `Icy-MetaData`
- `icy-metaint`

### ICY metadata

Internet radio streams can contain metadata interleaved with audio data.

The client implements a state machine which separates:

```text
Audio → Metadata length → Metadata → Audio → ...
```

Audio data is passed directly to `stdout`, while extracted metadata is written to `stderr`.

This makes the application composable with standard command-line audio players:

```bash
./sikradio -u <radio-url> -m | mpv -
```

### Timeouts and reconnection

The client distinguishes between:

- successful streaming
- receive timeouts
- server-side connection closure
- client-requested shutdown

After a receive timeout, the connection is closed and the client attempts to establish the stream again.

### Concurrent shutdown

The application uses:

- `std::thread`
- `std::atomic`
- POSIX `poll()`
- `shutdown()`

A dedicated thread monitors `stdin` for the `quit` command while the main thread handles the network connection and audio stream.

---

## Testing

The test suite uses **Python `unittest`** and starts the actual `sikradio` executable against locally implemented TCP mock servers.

This allows network behaviour to be tested without depending on external radio stations.

The tests cover, among other things:

- command-line argument validation
- IPv4 / IPv6 options
- HTTP responses
- redirects
- cookie propagation
- audio forwarding
- byte-level audio integrity
- large streams
- receive timeouts
- reconnection
- graceful shutdown
- malformed HTTP responses
- large headers
- connection failures
- ICY metadata
- zero-length metadata
- large metadata blocks
- slow/drip-feed connections
- logging verbosity

Example:

```bash
make test
```

---

## Continuous Integration

The repository includes a **GitHub Actions workflow** which automatically builds and tests the project.

---

## Architecture

The implementation is split into several focused components:

| Component | Responsibility |
|---|---|
| `url_parser` | URL parsing and validation |
| `network_logic` | DNS resolution, socket creation and connection handling |
| `IStream` | Common transport interface |
| `tcp_stream` | TCP transport |
| `tls_stream` | TLS transport using OpenSSL |
| `http_logic` | HTTP requests, responses, redirects and cookies |
| `client_config` | Command-line configuration and validation |
| `logger` | Configurable diagnostic logging |
| `sikradio` | Application control flow and stream processing |
| `tests/test_radio.py` | Automated network/integration tests |

The separation between transport and protocol logic allows the same HTTP layer to operate over both plain TCP and TLS.

---

## CLI

```text
./sikradio -u <URL> [-4] [-6] [-t <timeout>] [-m] [-v <level>] [-q]
```

### Options

| Option | Description |
|---|---|
| `-u <url>` | Radio stream URL |
| `-m` | Request ICY metadata |
| `-t <timeout>` | Receive timeout in milliseconds |
| `-4` | Force IPv4 |
| `-6` | Force IPv6 |
| `-v <level>` | Configure logging verbosity |
| `-q` | Disable logging |

### Examples

Basic streaming:

```bash
./sikradio -u http://example.com/radio
```

HTTPS with ICY metadata:

```bash
./sikradio -u https://example.com/radio -m
```

Force IPv4:

```bash
./sikradio -u https://example.com/radio -4
```

Force IPv6:

```bash
./sikradio -u https://example.com/radio -6
```

Pipe the audio stream to an external player:

```bash
./sikradio -u https://example.com/radio | mpv -
```

---

## Build

### Requirements

- C++20-compatible compiler
- GNU Make
- OpenSSL development libraries
- Python 3

The project is compiled with strict warning flags including:

```text
-Wall
-Wextra
-Wconversion
-Wsign-conversion
-Werror
```

Build:

```bash
make
```

Run tests:

```bash
make test
```

Clean:

```bash
make clean
```

---

## Project Structure

```text
.
├── IStream.h
├── client_config.cpp
├── client_config.h
├── http_logic.cpp
├── http_logic.h
├── logger.cpp
├── logger.h
├── network_logic.cpp
├── network_logic.h
├── sikradio.cpp
├── tcp_stream.cpp
├── tcp_stream.h
├── tls_stream.cpp
├── tls_stream.h
├── url_parser.cpp
├── url_parser.h
├── Makefile
├── examples/
└── tests/
    └── test_radio.py
```

---

## Technical Focus

- **C++20**
- **Network programming**
- **POSIX sockets**
- **TCP/IP**
- **IPv4 / IPv6**
- **DNS and `getaddrinfo()`**
- **HTTP**
- **HTTPS**
- **TLS / OpenSSL**
- **Protocol parsing**
- **Streaming data processing**
- **Concurrency and synchronization**
- **RAII and smart pointers**
- **Error handling**
- **Automated integration testing**
- **Continuous Integration**

---

## Academic Context

Originally developed as part of the **Computer Networks (Sieci Komputerowe)** course at the **University of Warsaw**.
