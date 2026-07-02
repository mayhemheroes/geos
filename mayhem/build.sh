#!/usr/bin/env bash
#
# geos/mayhem/build.sh — build libgeos/geos' OSS-Fuzz harness (fuzz_geo2) as a sanitized
# libFuzzer target (+ a standalone reproducer), instrumenting the GEOS library itself.
#
# Fuzzed surface (see mayhem/harnesses/fuzz_geo2.c): the GEOS C API geometry parsers and
# overlay engine — GEOSGeomFromWKT (WKT reader), GEOSGeomFromWKB_buf (WKB reader),
# GEOSGeomToWKT/GEOSGeomToWKB_buf (writers) and GEOSIntersection/Difference/Union.
# Input layout is  [WKT-text] \0 [WKB-bytes].
#
# Build contract comes from the org base ENV (CC/CXX/SANITIZER_FLAGS/LIB_FUZZING_ENGINE/
# SRC/STANDALONE_FUZZ_MAIN). We build the GEOS static libs WITH $SANITIZER_FLAGS so the
# parser/overlay code (not just the harness) is instrumented, then link the harness against them.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' — must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# `=` (not `:=`) for SANITIZER_FLAGS so an explicit empty --build-arg builds with NO sanitizers.
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS: always emit DWARF ≤ 3 symbols (clang-19 defaults to DWARF-5; §6.2 item 10).
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"

# ROOT CAUSE FIX: -fsanitize=fuzzer-no-link must be present at COMPILE time so clang injects
# the sancov guard/counter sections (__sancov_cntrs, __sancov_pcs) into every TU. Passing
# LIB_FUZZING_ENGINE (-fsanitize=fuzzer) only at link time adds the fuzzer runtime but does NOT
# back-instrument already-compiled objects — without compile-time -fsanitize=fuzzer-no-link the
# binary has zero edges and Mayhem reports a 0-edge run.
# CFLAGS_FUZZER is appended to every CC/CXX compile invocation for the instrumented build tree
# (libgeos + harness).  The TESTBUILD/oracle tree uses `env -u SANITIZER_FLAGS` so it is
# unaffected by this variable.
CFLAGS_FUZZER="-fsanitize=fuzzer-no-link"

export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS CFLAGS_FUZZER

cd "$SRC"

HARNESS_DIR="$SRC/mayhem/harnesses"
OUT="${OUT:-/mayhem}"
mkdir -p "$OUT"

# ── 1) Build the GEOS static libs WITH sanitizers (the fuzzed parsers are instrumented) ─────────
# CMake is GEOS' supported build system. Static libs (BUILD_SHARED_LIBS=OFF) so the harness is
# self-contained. We push $SANITIZER_FLAGS into both C and C++ flags. Disasble the (huge) test/
# benchmark trees here — those are built separately by mayhem/build.sh's test stage / test.sh.
BUILD="$SRC/mayhem-build"
cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_BENCHMARKS=OFF \
  -DBUILD_GEOSOP=OFF \
  -DBUILD_DOCUMENTATION=OFF \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_C_FLAGS="$SANITIZER_FLAGS $CFLAGS_FUZZER $DEBUG_FLAGS" \
  -DCMAKE_CXX_FLAGS="$SANITIZER_FLAGS $CFLAGS_FUZZER $DEBUG_FLAGS"

# Build the C API lib (geos_c) and its deps (geos / ryu / etc) — this also generates geos_c.h.
cmake --build "$BUILD" --target geos_c -j"$MAYHEM_JOBS"

# Locate the generated geos_c.h (CMake configures capi/geos_c.h.in into the build tree).
GEOS_C_H="$(find "$BUILD" -name geos_c.h -print -quit)"
if [ -z "$GEOS_C_H" ]; then
  echo "ERROR: generated geos_c.h not found under $BUILD" >&2
  exit 1
fi
INC="-I$(dirname "$GEOS_C_H") -I$SRC/include -I$SRC/capi"

# Collect the produced static archives (geos_c + geos + any third-party static libs like ryu).
mapfile -t GEOS_LIBS < <(find "$BUILD" -name '*.a' | sort)
if [ "${#GEOS_LIBS[@]}" -eq 0 ]; then
  echo "ERROR: no GEOS static libraries (*.a) produced under $BUILD" >&2
  exit 1
fi
echo "GEOS static libs:"; printf '  %s\n' "${GEOS_LIBS[@]}"

# ── 2) Standalone driver object (no libFuzzer runtime, run-once on one input file) ──────────────
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$HARNESS_DIR/standalone_main.c" -o "$BUILD/standalone_main.o"

# ── 3) Build the harness twice: libFuzzer target + standalone reproducer ────────────────────────
# Compile the harness with $CC (C mode) so LLVMFuzzerTestOneInput keeps C linkage (libFuzzer's
# FuzzerMain references the un-mangled symbol). LINK with $CXX so libstdc++ is pulled in for the
# C++ GEOS libs. --start-group/--end-group resolves the cyclic deps between geos_c / geos archives.
LINK_LIBS=(-Wl,--start-group "${GEOS_LIBS[@]}" -Wl,--end-group -lm)

for harness in fuzz_geo2; do
  $CC $SANITIZER_FLAGS $CFLAGS_FUZZER $DEBUG_FLAGS $INC -c "$HARNESS_DIR/$harness.c" -o "$BUILD/$harness.o"

  # libFuzzer target -> $OUT/<name>
  $CXX $SANITIZER_FLAGS $CFLAGS_FUZZER $DEBUG_FLAGS \
      "$BUILD/$harness.o" $LIB_FUZZING_ENGINE "${LINK_LIBS[@]}" \
      -o "$OUT/$harness"

  # standalone reproducer (no libFuzzer runtime) -> $OUT/<name>-standalone
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS \
      "$BUILD/$harness.o" "$BUILD/standalone_main.o" "${LINK_LIBS[@]}" \
      -o "$OUT/$harness-standalone"

  echo "built $harness (+ standalone)"
done

# ── 4) Build GEOS' OWN ctest unit suite with NORMAL flags (clean tree) so mayhem/test.sh
#       only RUNS it. No sanitizers here — keeps test.sh an honest PATCH oracle. ────────────────
TESTBUILD="$SRC/mayhem-tests"
env -u CFLAGS -u CXXFLAGS -u SANITIZER_FLAGS \
  cmake -S "$SRC" -B "$TESTBUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTING=ON \
    -DBUILD_BENCHMARKS=OFF \
    -DBUILD_DOCUMENTATION=OFF
# Build only the unit-test runner (test_geos_unit) + its deps; the xmltester needs external XML
# data we don't ship, so we build/run only the self-contained unit suite (its --data dir,
# tests/resources, is in-repo so the unit tests need no external download).
env -u CFLAGS -u CXXFLAGS -u SANITIZER_FLAGS \
  cmake --build "$TESTBUILD" --target test_geos_unit -j"$MAYHEM_JOBS"
echo "built geos unit test suite in mayhem-tests/"

# ── 5) Build the behavioral oracle: geos_oracle checks GEOS C API known-answer results. ─────────
# Compiled with NORMAL flags (no sanitizers) like the rest of the test suite, so it stays an
# honest PATCH oracle. Links against the Release geos_c static libs from the TESTBUILD tree.
mapfile -t TEST_LIBS < <(find "$TESTBUILD" -name '*.a' | sort)
TESTBUILD_C_H="$(find "$TESTBUILD" -name geos_c.h -print -quit)"
TEST_INC="-I$(dirname "$TESTBUILD_C_H") -I$SRC/include -I$SRC/capi"
env -u CFLAGS -u CXXFLAGS -u SANITIZER_FLAGS \
  cc $TEST_INC -O2 -Wall \
    "$SRC/mayhem/testsuite/geos_oracle.c" \
    -Wl,--start-group "${TEST_LIBS[@]}" -Wl,--end-group -lm -lstdc++ \
    -o "$OUT/geos_oracle"
echo "built geos_oracle (behavioral test oracle)"

echo "build.sh complete:"
ls -la "$OUT/fuzz_geo2" "$OUT/fuzz_geo2-standalone" "$OUT/geos_oracle" 2>&1 || true
