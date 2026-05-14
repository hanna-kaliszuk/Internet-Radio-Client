CXX = g++

CXXFLAGS = -Wall -Wextra -O2 -std=c++20
LDFLAGS = -lssl -lcrypto

TARGET = sikradio 

SRCS = sikradio.cpp client_config.cpp url_parser.cpp network_logic.cpp http_logic.cpp tcp_stream.cpp tls_stream.cpp

OBJS = $(SRCS:.cpp=.o)

.PHONY: all clean 

all: $(TARGET)

$(TARGET): $(OBJS)
		$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.cpp
		$(CXX) $(CXXFLAGS) -c $< -o $@

clean: 
		rm -f $(OBJS) $(TARGET)