#!/usr/bin/env bash
set -Eeuo pipefail
CAMERA_SOURCE=${1:?source checkout}
CAMERA_BUILD=${2:?build output}
CAMERA_SDK=/bigdata/android-sdk
CAMERA_NDK=$CAMERA_SDK/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin
CAMERA_TOOLS=$CAMERA_SDK/build-tools/36.0.0
CAMERA_JAR=$CAMERA_SDK/platforms/android-36/android.jar
CAMERA_SIGNING=/bigdata/linux-camera-routing/signing
test "$(uname -m)" = x86_64
mkdir -p "$CAMERA_BUILD/native" "$CAMERA_BUILD/tests" "$CAMERA_BUILD/android/"{generated,classes,dex,jni/lib/arm64-v8a}
CAMERA_CFLAGS=(-std=c99 -O2 -Wall -Wextra -Werror)
gcc -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined "$CAMERA_SOURCE/packing.c" "$CAMERA_SOURCE/tests/packing_test.c" -o "$CAMERA_BUILD/tests/packing"
"$CAMERA_BUILD/tests/packing" > "$CAMERA_BUILD/tests/packing.log"
"$CAMERA_NDK/aarch64-linux-android35-clang" "${CAMERA_CFLAGS[@]}" -fPIE -pie "$CAMERA_SOURCE/broker.c" "$CAMERA_SOURCE/device.c" -o "$CAMERA_BUILD/native/linux-camerad"
"$CAMERA_NDK/aarch64-linux-android35-clang" "${CAMERA_CFLAGS[@]}" -fPIE -pie "$CAMERA_SOURCE/tests/lifecycle.c" "$CAMERA_SOURCE/device.c" -o "$CAMERA_BUILD/native/camera-lifecycle-test"
"$CAMERA_NDK/aarch64-linux-android35-clang" "${CAMERA_CFLAGS[@]}" -fPIE -pie "$CAMERA_SOURCE/tests/measure.c" -o "$CAMERA_BUILD/native/camera-measure"
"$CAMERA_NDK/aarch64-linux-android35-clang" "${CAMERA_CFLAGS[@]}" -fPIC -shared -Wl,-z,max-page-size=16384 "$CAMERA_SOURCE/capture.c" "$CAMERA_SOURCE/packing.c" -landroid -lmediandk -llog -o "$CAMERA_BUILD/android/jni/lib/arm64-v8a/liblinux_camera.so"
bash "$CAMERA_SOURCE/build-support/kernel.sh" "$CAMERA_SOURCE" "$CAMERA_BUILD"
"$CAMERA_TOOLS/aapt2" compile --dir "$CAMERA_SOURCE/android/app/src/main/res" -o "$CAMERA_BUILD/android/resources.zip"
"$CAMERA_TOOLS/aapt2" link -o "$CAMERA_BUILD/android/base.apk" -I "$CAMERA_JAR" --manifest "$CAMERA_SOURCE/android/app/src/main/AndroidManifest.xml" --java "$CAMERA_BUILD/android/generated" --version-code 3 --version-name 0.1.2 "$CAMERA_BUILD/android/resources.zip"
mapfile -t CAMERA_JAVA < <(find "$CAMERA_SOURCE/android/app/src/main/java" "$CAMERA_BUILD/android/generated" -name '*.java' -type f)
javac --release 17 -cp "$CAMERA_JAR" -d "$CAMERA_BUILD/android/classes" "${CAMERA_JAVA[@]}"
mapfile -t CAMERA_CLASSES < <(find "$CAMERA_BUILD/android/classes" -name '*.class' -type f)
"$CAMERA_TOOLS/d8" --min-api 35 --lib "$CAMERA_JAR" --output "$CAMERA_BUILD/android/dex" "${CAMERA_CLASSES[@]}"
cp "$CAMERA_BUILD/android/base.apk" "$CAMERA_BUILD/android/unsigned.apk"
(cd "$CAMERA_BUILD/android/dex" && zip -q "$CAMERA_BUILD/android/unsigned.apk" classes*.dex)
(cd "$CAMERA_BUILD/android/jni" && zip -0 -q "$CAMERA_BUILD/android/unsigned.apk" lib/arm64-v8a/liblinux_camera.so)
"$CAMERA_TOOLS/zipalign" -P 16 -f 4 "$CAMERA_BUILD/android/unsigned.apk" "$CAMERA_BUILD/android/aligned.apk"
if [[ ! -e "$CAMERA_SIGNING/linux-camera.keystore" ]]; then
    install -d -m 0700 "$CAMERA_SIGNING"
    umask 077
    openssl rand -hex 24 > "$CAMERA_SIGNING/password"
    keytool -genkeypair -keystore "$CAMERA_SIGNING/linux-camera.keystore" -storepass:file "$CAMERA_SIGNING/password" -keypass:file "$CAMERA_SIGNING/password" -alias linux-camera -dname 'CN=KiralyCraft Linux Camera' -keyalg RSA -keysize 4096 -validity 10950 > "$CAMERA_BUILD/android/signing.log" 2>&1
fi
"$CAMERA_TOOLS/apksigner" sign --ks "$CAMERA_SIGNING/linux-camera.keystore" --ks-pass "file:$CAMERA_SIGNING/password" --out "$CAMERA_BUILD/android/LinuxCamera-0.1.2.apk" "$CAMERA_BUILD/android/aligned.apk"
"$CAMERA_TOOLS/apksigner" verify --verbose --print-certs "$CAMERA_BUILD/android/LinuxCamera-0.1.2.apk" > "$CAMERA_BUILD/android/verification.txt"
