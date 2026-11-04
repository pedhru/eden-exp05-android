#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
: "${ANDROID_HOME:?Android SDK required}"
ndk="$ANDROID_HOME/ndk/28.2.13676358"
bt="$ANDROID_HOME/build-tools/35.0.0"
mkdir -p artifacts/native artifacts/logs
exec > >(tee artifacts/logs/build.log) 2>&1
export ASAN_OPTIONS=detect_leaks=0
bash .ci/android/test-exp06-host.sh
python3 .ci/android/test-nx2-nce.py --clang "$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
bash .ci/android/build-nce-native.sh "$ndk" "$root/.venv" "$root/build-exp06"
cp build-exp06/out/libyuzu-android.so artifacts/libyuzu-android-unstripped.so
cp build-exp06/out/libyuzu-android.so artifacts/native/
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded artifacts/native/libyuzu-android.so
python3 .ci/android/repack-exp06.py --base ci-inputs/Eden_EXP04.apk --native artifacts/native --out artifacts/unsigned.apk
"$bt/zipalign" -P 16 -f 4 artifacts/unsigned.apk artifacts/aligned.apk
java -jar "$bt/lib/apksigner.jar" sign --ks src/android/app/debug.keystore --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out artifacts/Eden_NX2_Android_EXP06.apk artifacts/aligned.apk
python3 .ci/android/verify-exp06-apk.py --apk artifacts/Eden_NX2_Android_EXP06.apk --base ci-inputs/Eden_EXP04.apk --ndk "$ndk" --build-tools "$bt" --java "$(command -v java)" --report artifacts/APK_verification.json
rm artifacts/unsigned.apk artifacts/aligned.apk
(cd artifacts && sha256sum Eden_NX2_Android_EXP06.apk libyuzu-android-unstripped.so > SHA256SUMS.txt)
