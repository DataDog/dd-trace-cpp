SOURCES := $(wildcard ../src/*.cpp)
HEADERS := $(wildcard ../src/*.h)

example: example.cpp $(SOURCES) $(HEADERS)
	$(CXX) --std=c++17 -g -Wall -Wextra -pedantic -Werror -I../include -o $@ $< $(SOURCES)
