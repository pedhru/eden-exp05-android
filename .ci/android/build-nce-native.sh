#!/usr/bin/env bash
set -euo pipefail
ndk_dir="$(realpath "$1")"
cmake_dir="$(realpath "$2")"
build_dir="$(realpath -m "$3")"
source_dir="$(cd "$(dirname "$0")/../.." && pwd)"
"$cmake_dir/bin/cmake" -S "$source_dir" -B "$build_dir/build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ndk_dir/build/cmake/android.toolchain.cmake" \
  -DCMAKE_MAKE_PROGRAM="$cmake_dir/bin/ninja" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-33 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$build_dir/out" \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$build_dir/out" \
  -DENABLE_QT=OFF -DENABLE_WEB_SERVICE=ON -DANDROID_ARM_NEON=ON \
  -DYUZU_USE_CPM=ON -DCPMUTIL_FORCE_BUNDLED=ON -DYUZU_USE_BUNDLED_FFMPEG=ON \
  -DBUILD_TESTING=OFF -DYUZU_TESTS=OFF -DDYNARMIC_TESTS=OFF \
  -DENABLE_WERROR=OFF -DENABLE_UPDATE_CHECKER=OFF -DENABLE_LTO=OFF \
  -DYUZU_DOWNLOAD_ANDROID_VVL=OFF -DEDEN_NX2_NO_ENV_FILE=ON
"$cmake_dir/bin/cmake" --build "$build_dir/build" --target yuzu-android --parallel "${BUILD_JOBS:-2}"
