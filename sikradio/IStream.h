#pragma once

#include <sys/types.h>

class IStream {
public:
    virtual ~IStream() = default;

    virtual ssize_t read(void* buf, size_t count) = 0;
    virtual ssize_t write(const void* buf, size_t count) = 0;
    virtual void close() = 0;
    virtual int get_fd() const = 0;
};

enum class StreamState {
    AUDIO,
    MULTIPLIER, METADATA
};

enum class StreamResult {
    OK,
    TIMEOUT,
    CLOSED_BY_SERVER,
    STOPPED_BY_CLIENT
};