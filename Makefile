CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
TARGET = chip8
SOURCES = src/main.cpp src/chip8.cpp src/network.cpp src/lan_game.cpp
OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) -lSDL2 -lSDL2_ttf

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean