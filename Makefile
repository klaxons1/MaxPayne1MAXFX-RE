# Minimal GNU Make build for the parser / dump tool (no SDL required).
# Level viewer:
#   cmake -S . -B build && cmake --build build
# (see CMakeLists.txt / README.md)

CXX      ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra -I src -I third_party
LDFLAGS  ?=

DUMP_SRCS = \
	src/maxfx/core/Stream.cpp \
	src/maxfx/core/Fs.cpp \
	src/maxfx/ldb/LdbReader.cpp \
	src/maxfx/image/Image.cpp \
	src/apps/ldb_dump.cpp

.PHONY: all dump clean

all: dump

dump: ldb-dump

ldb-dump: $(DUMP_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(DUMP_SRCS) $(LDFLAGS) -lm

clean:
	rm -f ldb-dump ldb-viewer
	rm -rf build
