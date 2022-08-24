SOURCES := $(wildcard ../src/*.cpp)
HEADERS := $(wildcard ../src/*.h) ../src/json.hpp

example: example.cpp $(SOURCES) $(HEADERS)
	$(CXX) \
		--std=c++17 \
		-g \
		-Og \
		-Wall -Wextra -pedantic -Werror -Wno-non-template-friend \
		-I../include \
		-o $@ \
		$< $(SOURCES) \
		-L/usr/local/lib -lcurl

hasher: hasher.cpp $(SOURCES) $(HEADERS)
	$(CXX) \
		--std=c++17 \
		-g \
		-Og \
		-Wall -Wextra -pedantic -Werror -Wno-non-template-friend \
		-I../include \
		-o $@ \
		$< $(SOURCES) \
		-L/usr/local/lib -lcurl