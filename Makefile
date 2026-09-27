# Minimal GNU Make build for the parsers / dump tools (no SDL required).
#   make dump     → ldb-dump, levels-dump, levels-test
#   make test     → run the levels.txt self-test
# Level viewer:
#   cmake -S . -B build && cmake --build build
# (see CMakeLists.txt / README.md)

CXX      ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra -I src -I third_party
LDFLAGS  ?=

CORE_SRCS = \
	src/maxfx/core/Stream.cpp \
	src/maxfx/core/Fs.cpp \
	src/maxfx/script/Script.cpp \
	src/maxfx/levels/Levels.cpp \
	src/maxfx/ldb/LdbReader.cpp \
	src/maxfx/image/Image.cpp \
	src/maxfx/kf2/Kf2.cpp \
	src/maxfx/db/Database.cpp \
	src/maxfx/sound/Sound.cpp \
	src/maxfx/collision/Collision.cpp \
	src/maxfx/char/Character.cpp \
	src/maxfx/game/Message.cpp \
	src/maxfx/game/Catalog.cpp \
	src/maxfx/game/Runtime.cpp \
	src/maxfx/game/Effects.cpp \
	src/maxfx/game/CameraPaths.cpp \
	src/maxfx/game/Hud.cpp

DUMP_SRCS = $(CORE_SRCS) src/apps/ldb_dump.cpp
LEVELS_DUMP_SRCS = $(CORE_SRCS) src/apps/levels_dump.cpp
LEVELS_TEST_SRCS = $(CORE_SRCS) src/apps/levels_test.cpp

.PHONY: all dump test clean

all: dump

dump: ldb-dump levels-dump levels-test

ldb-dump: $(DUMP_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(DUMP_SRCS) $(LDFLAGS) -lm

levels-dump: $(LEVELS_DUMP_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(LEVELS_DUMP_SRCS) $(LDFLAGS) -lm

levels-test: $(LEVELS_TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(LEVELS_TEST_SRCS) $(LDFLAGS) -lm

test: levels-test
	./levels-test

clean:
	rm -f ldb-dump ldb-viewer levels-dump levels-test
	rm -rf build
