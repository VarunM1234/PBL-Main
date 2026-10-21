# =====================================================================
# SmartRoute Makefile (C17 core + C++17 app). Works on Linux, macOS and
# Windows MSYS2 / MinGW-w64 UCRT64 (use the "UCRT64" shell and run "make").
# =====================================================================
CC  ?= gcc
CXX ?= g++
WARN := -Wall -Wextra -Wpedantic -Werror
EXTRA    :=
CFLAGS   := -std=c17   $(WARN) -O2 -Icore -MMD -MP $(EXTRA)
CXXFLAGS := -std=c++17 $(WARN) -O2 -Icore -Iapp -MMD -MP $(EXTRA)
LDFLAGS  := -lm $(EXTRA)
ifeq ($(OS),Windows_NT)
LDFLAGS  += -lws2_32
EXE := .exe
# Native Windows make would run every recipe through cmd.exe, which has no
# mkdir -p / od / sed / rm and breaks the web page -> byte-array step.
# Git Bash and MSYS2 both put sh on PATH, so use it for the recipes.
SHELL := sh
endif
# Sanitizer build flags. MSYS2 / MinGW-w64 GCC ships NEITHER libasan nor libubsan (and clang's
# compiler-rt is not installed here), so hard-coding -fsanitize would make "make debug" die in ld
# on Windows instead of ever running the fuzzed inputs. Probe once at parse time: if the runtime
# links, use ASan + UBSan and abort on any report; otherwise fall back to the strict checks that
# this compiler CAN link (-O0 debug info, libstdc++ container assertions, stack protector).
# This probe always writes into build/ (a literal, because it runs before BUILD/BIN are set below).
SAN_OK := $(shell mkdir -p build 2>/dev/null; \
   printf 'int main(void){return 0;}' > build/.san_probe.c; \
   $(CC) -fsanitize=address,undefined build/.san_probe.c -o build/.san_probe$(EXE) >/dev/null 2>&1 && echo yes || echo no; \
   rm -f build/.san_probe.c build/.san_probe$(EXE))
ifeq ($(SAN_OK),yes)
DEBUG_FLAGS := -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
SAN_MODE := ASan + UBSan
else
DEBUG_FLAGS := -g -O0 -D_GLIBCXX_ASSERTIONS -fstack-protector-strong -fno-omit-frame-pointer
SAN_MODE := debug fallback (this compiler has no sanitizer runtime - run "make debug" on Linux for ASan + UBSan)
endif

BUILD := build
BIN   := bin
CORE_SRCS := $(wildcard core/*.c)
CORE_OBJS := $(patsubst core/%.c,$(BUILD)/core/%.o,$(CORE_SRCS))
APP_SRCS  := $(filter-out app/main.cpp app/http_server.cpp,$(wildcard app/*.cpp))
APP_OBJS  := $(patsubst app/%.cpp,$(BUILD)/app/%.o,$(APP_SRCS))
ALL_OBJS  := $(CORE_OBJS) $(APP_OBJS) $(BUILD)/app/main.o $(BUILD)/app/http_server.o $(BUILD)/web_page.o \
             $(BUILD)/tests/test_core.o $(BUILD)/tests/test_core2.o $(BUILD)/tests/test_app.o \
             $(BUILD)/tests/test_ops.o $(BUILD)/tests/bench.o

.PHONY: all test check-tools clean debug debug-server run demo bench serve dlls onefile run1 demo1 serve1 test-ops basemap
all: $(BIN)/smartroute$(EXE) dlls

# On Windows the exe loads libstdc++/libgcc/libwinpthread through PATH, and an old
# MinGW installation can shadow the ones this compiler needs (instant segfault).
# Copy the matching DLLs from the compiler's own bin directory next to the exes:
# Windows always loads the application-directory copy first, so bin/ runs safely from any shell.
ifeq ($(OS),Windows_NT)
GCCBINDIR := $(shell dirname "$$(command -v gcc)")
dlls:
	@mkdir -p "$(BIN)"
	@for d in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do \
	  if [ -f "$(GCCBINDIR)/$$d" ]; then cp -f "$(GCCBINDIR)/$$d" "$(BIN)/"; fi; done
else
dlls:
	@true
endif

check-tools:
	@echo "Checking build tools..."
	@$(CC) --version | head -n 1
	@$(CXX) --version | head -n 1
	@make --version | head -n 1

$(BUILD)/core/%.o: core/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/app/%.o: app/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# The delivered page is web/index.html with web/vendor/leaflet.{css,js} inlined by
# tools/page_gen.cpp (Leaflet 1.9.4, BSD-2-Clause - the project's only third-party
# exception, licence kept next to it). That single HTML file is then turned into a
# C++ byte array, all with g++ - no Python, no Node (od/sed would choke on the size).
$(BIN)/page_gen$(EXE): tools/page_gen.cpp
	@mkdir -p $(BIN)
	@$(CXX) -std=c++17 $(WARN) -Wno-sign-compare -O2 $< -o $@
$(BUILD)/web_page.html: $(BIN)/page_gen$(EXE) web/index.html web/vendor/leaflet.css web/vendor/leaflet.js
	@mkdir -p $(BUILD)
	@$(BIN)/page_gen$(EXE) . html
$(BUILD)/web_page.cpp: $(BUILD)/web_page.html
	@mkdir -p $(BUILD)
	@$(BIN)/page_gen$(EXE) . bytes $@

$(BUILD)/web_page.o: $(BUILD)/web_page.cpp
	@mkdir -p $(BUILD)
	$(CXX) -std=c++17 -O2 -c $< -o $@

$(BIN)/smartroute$(EXE): $(CORE_OBJS) $(APP_OBJS) $(BUILD)/app/main.o $(BUILD)/app/http_server.o $(BUILD)/web_page.o
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)
$(BIN)/test_core$(EXE): $(BUILD)/tests/test_core.o $(CORE_OBJS)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS)
$(BIN)/test_core2$(EXE): $(BUILD)/tests/test_core2.o $(CORE_OBJS)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS)
$(BIN)/test_app$(EXE): $(BUILD)/tests/test_app.o $(CORE_OBJS) $(APP_OBJS)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)
# The staff flow over real HTTP: it starts the same server the app uses, on port 3199,
# in this process and talks to it through a socket - no Node, no test framework.
$(BIN)/test_ops$(EXE): $(BUILD)/tests/test_ops.o $(CORE_OBJS) $(APP_OBJS) $(BUILD)/app/http_server.o $(BUILD)/web_page.o
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)
$(BIN)/bench$(EXE): $(BUILD)/tests/bench.o $(CORE_OBJS)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

test: $(BIN)/test_core$(EXE) $(BIN)/test_core2$(EXE) $(BIN)/test_app$(EXE) dlls
	@$(BIN)/test_core$(EXE)
	@$(BIN)/test_core2$(EXE)
	@$(BIN)/test_app$(EXE)

test-ops: $(BIN)/test_ops$(EXE) dlls
	@$(BIN)/test_ops$(EXE)

# The offline map background (spec 8): web/basemap.json, generated by the program itself from the catalogue
# (2-degree grid + every corridor + a labelled dot per city). Add real coastlines with a land GeoJSON:
#   make basemap LAND=ne_110m_land.geojson
# The server answers GET /basemap.json with this file when it exists and with the same document generated on
# the spot when it does not, so a fresh checkout still has a background when the tile servers are blocked.
basemap: $(BIN)/smartroute$(EXE)
	@if [ -n "$(LAND)" ]; then \
	  $(BIN)/smartroute$(EXE) --make-basemap "$(LAND)" web/basemap.json; \
	else \
	  $(BIN)/smartroute$(EXE) --make-basemap web/basemap.json; \
	fi

bench: $(BIN)/bench$(EXE)
	@$(BIN)/bench$(EXE)

run: all
	@$(BIN)/smartroute$(EXE)
demo: all
	@$(BIN)/smartroute$(EXE) --demo
serve: all
	@$(BIN)/smartroute$(EXE) --serve 8080 --open

# One-file build: main.cpp contains the ENTIRE project (core + app + web server + embedded page).
# It is generated by tools/onefile.cpp - a C++ program, so the whole repo builds with g++ and make only.
onefile: $(BIN)/smartroute_onefile$(EXE) dlls
$(BIN)/smartroute_onefile$(EXE): main.cpp
	@mkdir -p $(BIN)
	@$(CXX) -std=c++17 $(WARN) -O2 main.cpp -o $@ $(LDFLAGS)
$(BIN)/onefile_gen$(EXE): tools/onefile.cpp
	@mkdir -p $(BIN)
	@$(CXX) -std=c++17 $(WARN) -O2 $< -o $@ $(LDFLAGS)
main.cpp: $(wildcard core/*.c core/*.h app/*.cpp app/*.hpp) $(BUILD)/web_page.html $(BIN)/onefile_gen$(EXE)
	@$(BIN)/onefile_gen$(EXE) .
run1: onefile
	@$(BIN)/smartroute_onefile$(EXE)
demo1: onefile
	@$(BIN)/smartroute_onefile$(EXE) --demo --no-save
serve1: onefile
	@$(BIN)/smartroute_onefile$(EXE) --serve 8080 --open

# Sanitizer build (ASan + UBSan on Linux/macOS; the strict fallback above on MinGW GCC). Uses
# separate folders so it never mixes with the normal build. It builds the TESTS and the SERVER,
# so a suspicious web session can be re-run under the same flags ("make debug-server", port 8081).
debug:
	@echo "make debug: checks = $(SAN_MODE)"
	$(MAKE) BUILD=build-debug BIN=bin-debug EXTRA="$(DEBUG_FLAGS)" test
	@$(MAKE) BUILD=build-debug BIN=bin-debug EXTRA="$(DEBUG_FLAGS)" bin-debug/smartroute$(EXE)
	@echo "sanitizer server built too - run it with: bin-debug/smartroute --serve 8081"
debug-server:
	@$(MAKE) BUILD=build-debug BIN=bin-debug EXTRA="$(DEBUG_FLAGS)" bin-debug/smartroute$(EXE)
	@echo "sanitizer server built - run it with: bin-debug/smartroute --serve 8081"

clean:
	rm -rf build bin build-debug bin-debug

# Every object depends on the Makefile: the flags live here (and "make debug" swaps them for
# sanitizer or hardened ones), so without this a stale build-debug/ from an earlier run would be
# relinked with the new flags and ld would fail on missing __asan/__ubsan symbols.
$(ALL_OBJS): Makefile

-include $(ALL_OBJS:.o=.d)
