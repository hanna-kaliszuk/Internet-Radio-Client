CXX := g++

CXXFLAGS := -Wall -Wextra -Wconversion -Werror -Wsign-conversion -O2 -std=c++20 \
            $(shell pkg-config --cflags openssl)

LDFLAGS := $(shell pkg-config --libs openssl)

TARGET := sikradio

SRCS := sikradio.cpp client_config.cpp url_parser.cpp network_logic.cpp \
        http_logic.cpp tcp_stream.cpp tls_stream.cpp logger.cpp

OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(TARGET)
	python3 -m unittest discover -s tests -p "test_*.py" -v

clean:
	rm -f $(OBJS) $(TARGET)