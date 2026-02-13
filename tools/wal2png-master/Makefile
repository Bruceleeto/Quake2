CXX = g++
CXXFLAGS = -std=c++20 -O2 -Wall

SRCS = src/main.cpp src/ByteReader.cpp src/lodepng.cpp src/pcx.cpp src/wal.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = wal2png

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)