.PHONY: all
all: ownership.svg includes.svg

%.svg: %.dot
	dot -Tsvg -o $@ $<

includes.dot: $(wildcard src/*)
	raco graph-includes --exclude-std-c --exclude-std-cpp --exclude-posix src/ >$@
