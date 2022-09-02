SOURCES := $(shell find ../src/ -type f -name '*.cpp' -not -name 'default_http_client_curl.cpp')
HEADERS := $(wildcard ../src/*.h) ../src/datadog/json.hpp

# "example" is built with cmake now.  See <../bin/example>.

hasher: hasher.cpp $(SOURCES) $(HEADERS)
	$(CXX) \
		--std=c++17 \
		-g \
		-Og \
		-Wall -Wextra -pedantic -Werror -Wno-non-template-friend \
		-I../src \
		-o $@ \
		$< $(SOURCES) \
		-L/usr/local/lib -lcurl