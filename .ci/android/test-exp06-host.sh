#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
temp="$(mktemp -d)"
trap 'rm -rf "$temp"' EXIT
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
for test in .ci/android/exp06-tests/*.cpp .ci/android/exp05-tests/nce_filter.cpp .ci/android/exp05-tests/pool_cleanup.cpp; do
    name="$(basename "$test" .cpp)"
    g++ -std=c++23 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Isrc "$test" -o "$temp/$name"
    "$temp/$name"
    echo "PASS $name"
done
