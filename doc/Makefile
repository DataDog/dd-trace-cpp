.PHONY: all
all: ownership.svg includes.svg

%.svg: %.dot
	dot -Grankdir=LR -Tsvg -o $@ $<

includes.dot: $(wildcard ../src/datadog/*)
	raco graph-includes --exclude-std-c --exclude-std-cpp --exclude-posix --extension h --extension cpp ../src/ >$@
