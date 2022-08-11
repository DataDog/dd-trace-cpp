SOURCES := $(wildcard ../src/*.cpp)
HEADERS := $(wildcard ../src/*.h)

example: example.cpp $(SOURCES) $(HEADERS)
	$(CXX) --std=c++17 -I../include -o $@ $< $(SOURCES)
