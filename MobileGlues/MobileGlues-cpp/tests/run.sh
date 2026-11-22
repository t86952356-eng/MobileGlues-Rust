#!/bin/sh
# Host-side checks for the parts of the pixel and framebuffer code that are pure
# enough to run without a GPU. Both link the real translation units, not copies.
#
#   sh MobileGlues-cpp/tests/run.sh
set -e
cd "$(dirname "$0")/.."
INC="-I. -I./includes -I./include -I./3rdparty/xxhash -I./3rdparty/ska"
CXX="${CXX:-g++} -std=gnu++20 -w $INC"
$CXX -o /tmp/mg_pixel_test  tests/pixel_size_test.cpp         gl/pixel.cpp
$CXX -o /tmp/mg_fb_test     tests/framebuffer_shuffle_test.cpp gl/framebuffer.cpp
/tmp/mg_pixel_test
echo
/tmp/mg_fb_test
echo
# Perf log smoke test: drives the counters and the swap stream and checks the
# report/history files come out complete. Takes ~12 s (it has to cross one
# 10-second report window to see the files written).
$CXX -o /tmp/mg_perf_smoke tests/perf_smoke_test.cpp gl/perf.cpp
/tmp/mg_perf_smoke
echo
# Pipeline cache store test: records, keys, binary/status round-trips, LRU
# budget and disk persistence (simulated restart via reload_for_test).
$CXX -o /tmp/mg_pipeline_cache tests/pipeline_cache_test.cpp gl/pipeline_cache.cpp
/tmp/mg_pipeline_cache
