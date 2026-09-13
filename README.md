# Internet Radio Client

A command-line Internet radio client implemented in C++ with direct socket communication, IPv4/IPv6 support, HTTP/HTTPS, TLS, streaming audio, and ICY metadata handling.

> Project for **Sieci Komputerowe** (Computer Networks), summer semester 2025/26, University of Warsaw.

## What is Internet Radio Client?

Internet Radio Client is a command-line application for receiving and playing Internet radio streams.

The project was built to implement a real network application from the ground up, working directly with the **POSIX sockets interface** instead of relying on high-level networking libraries. It combines several layers of network communication:

- DNS and address resolution,
- TCP connections,
- IPv4 and IPv6,
- HTTP request/response handling,
- HTTPS and TLS,
- streaming data,
- ICY radio metadata.

The application receives the raw audio stream and writes it to `stdout`, allowing an external audio player such as `mpv` to decode and play it. Diagnostic information and ICY metadata are written to `stderr`.

---

## Features

- TCP-based Internet radio streaming
- IPv4 and IPv6 support
- Automatic address resolution using `getaddrinfo`
- HTTP and HTTPS connections
- TLS communication using OpenSSL
- HTTP redirects
- Cookie handling
- ICY metadata extraction
- Configurable connection timeout
- Reconnection after a stalled connection
- Graceful shutdown on user request
- Concurrent monitoring of standard input
- Configurable logging verbosity
- Raw audio output suitable for external players
- Command-line argument parsing, including grouped options
- Automated unit and integration tests

---

## Technologies

### Networking

- **C++20**
- **POSIX sockets**
- **TCP/IP**
- **IPv4 / IPv6**
- **DNS / `getaddrinfo`**
- **HTTP**
- **TLS**
- **OpenSSL** (`libssl`, `libcrypto`)
- **`poll()`**

### C++ features

- RAII and smart pointers
- `std::thread`
- `std::atomic`
- Standard library containers and utilities
- Exception-based error handling

### Build tools

- **GNU Make**
- Compiler with C++20 support

The project is compiled with strict warning flags including `-Wall`, `-Wextra`, `-Wconversion`, `-Wsign-conversion`, and `-Werror`.

---

## Architecture

The networking layer is separated into stream abstractions and protocol logic.

```text
                         HTTP logic
                             │
                          IStream
                         /       \
                  TcpStream     TlsStream
                      │             │
                     TCP           TLS
```

`IStream` provides a common interface for reading from and writing to network connections. `TcpStream` handles plain TCP communication, while `TlsStream` provides encrypted communication using OpenSSL.

The higher-level HTTP logic can therefore work with both HTTP and HTTPS connections without duplicating the protocol implementation.

---

## Networking

The client parses the provided radio URL and determines the appropriate connection parameters.

The networking layer is responsible for:

- resolving hostnames with `getaddrinfo`,
- supporting both IPv4 and IPv6 addresses,
- trying resolved addresses when establishing a connection,
- selecting TCP or TLS depending on the URL,
- applying receive timeouts,
- detecting stalled connections,
- reconnecting when necessary.

IPv4 or IPv6 can also be explicitly selected through command-line options.

---

## HTTP and HTTPS

The client implements the HTTP communication required to establish and maintain a radio stream.

It handles:

- HTTP response status codes,
- response headers,
- redirects,
- cookies,
- streaming response bodies,
- ICY-specific headers,
- malformed or incomplete responses.

For HTTPS connections, the HTTP layer operates through `TlsStream`, which uses OpenSSL to establish the encrypted connection.

---

## ICY Metadata

Internet radio streams can contain metadata alongside the audio stream.

When metadata is requested, the client uses the `icy-metaint` value provided by the server to determine where metadata blocks occur in the stream.

The stream is processed as:

```text
Audio data → Metadata interval → Audio data → Metadata interval → ...
```

Audio data is written unchanged to `stdout`, while extracted metadata is sent to `stderr`.

This allows the audio stream to be piped directly into an external player without mixing protocol metadata into the audio data.

---

## Concurrent Shutdown

The client monitors standard input in a separate thread while the main thread handles network communication.

This allows the user to terminate a running stream without blocking the networking logic.

A shared atomic stop flag is used to coordinate shutdown between the threads, after which the active connection is closed cleanly.

---

## Command-Line Interface

The client supports the following options:

| Option | Description |
| :--- | :--- |
| `-u url` | Radio stream URL |
| `-m` | Request multiplexed ICY metadata |
| `-t timeout` | Connection timeout in milliseconds |
| `-4` | Force IPv4 |
| `-6` | Force IPv6 |
| `-v verbosity` | Set logging verbosity from 0 to 4 |
| `-q` | Quiet mode (`-v0`) |

Parameters can be supplied in different orders and selected short options can also be grouped.

Example:

```bash
./sikradio -u http://example.com/radio
```

With HTTPS and metadata:

```bash
./sikradio -u https://example.com/radio -m
```

The audio stream can be passed directly to an external player:

```bash
./sikradio -u https://example.com/radio | mpv -
```

---

## Building

### Prerequisites

- C++20-compatible compiler
- OpenSSL development libraries
- GNU Make

Build the project using:

```bash
make
```

This creates the `sikradio` executable.

To remove build artifacts:

```bash
make clean
```

To run the automated test suite:
```bash
make test
```

---

## Testing

The project includes an automated test suite covering both core functionality and network behaviour. 

Tests are implemented in Python using the standard `unittest` framework and are located in the `tests/` directory.

The test suite covers, among other things:

- command-line argument parsing,
- connection and server behaviour,
- HTTP and ICY responses,
- timeout handling,
- reconnection after stalled connections,
- signal handling and graceful shutdown,
- IPv4 and IPv6 communication,
- malformed and edge-case inputs.

Tests can be run using:

```bash
make test
```

---

## Running

Run the client with a radio stream URL:

```bash
./sikradio -u <url>
```

For example:

```bash
./sikradio -u https://example.com/radio -m -v2
```

The raw audio stream is written to `stdout`, while logs and metadata are written to `stderr`.

This makes it possible to pipe the audio directly to another program:

```bash
./sikradio -u <url> | mpv -
```

---

## Project Structure

```text
.
├── .gitignore
├── README.md
├── Makefile
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
└── examples/
    ├── sikradio_example_1.log
    ├── sikradio_example_2.log
    ├── sikradio_example_3.log
    ├── sikradio_example_4.log
    ├── sikradio_example_5.log
    ├── sikradio_example_6.log
    └── sikradio_example_7.log
└── tests/
    └── test_radio.py
```

The `examples/` directory contains example files provided by the assignment authors and used as part of the project specification and development process.

The `tests/` directory contains the automated Python test suite used to verify the client's behaviour.

---

## Useful Commands

### Build

```bash
make
```

### Remove build artifacts

```bash
make clean
```

### Run the client

```bash
./sikradio -u <url>
```

### Enable ICY metadata

```bash
./sikradio -u <url> -m
```

### Force IPv4

```bash
./sikradio -u <url> -4
```

### Force IPv6

```bash
./sikradio -u <url> -6
```

### Set verbosity

```bash
./sikradio -u <url> -v3
```

### Pipe the audio stream to `mpv`

```bash
./sikradio -u <url> | mpv -
```

---

## Notes

- The application is implemented without high-level networking libraries.
- Network communication is handled directly through the POSIX sockets interface.
- HTTP and HTTPS share the same higher-level stream logic through the `IStream` abstraction.
- The client supports both IPv4 and IPv6 connections.
- ICY metadata is separated from the raw audio stream and written to `stderr`.
- The project uses OpenSSL for TLS communication.
- The `examples/` directory contains files provided by the assignment authors.
- The project was developed as an educational implementation of a network application, with a focus on understanding how the different layers of communication work together.

---

## Course

This project was developed as a **course assignment for Computer Networks (SIK)** at the **University of Warsaw** during the **Summer Semester 2025/26**.