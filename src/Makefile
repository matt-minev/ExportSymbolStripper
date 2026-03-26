CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
TARGET = strip_symbol

all: $(TARGET)

$(TARGET): strip_symbol.cpp
	$(CXX) $(CXXFLAGS) strip_symbol.cpp -o $(TARGET)

clean:
	rm -f $(TARGET)
