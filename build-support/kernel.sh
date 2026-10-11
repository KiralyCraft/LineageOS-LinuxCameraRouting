#!/usr/bin/env bash
set -Eeuo pipefail
CAMERA_SOURCE=${1:?source}
CAMERA_BUILD=${2:?output}
CAMERA_EXACT=/bigdata/hdmi-los-build/work/companion-probe-20261005
CAMERA_CLANG=/bigdata/hdmi-los-build/cache/lineage-22.2-display/prebuilts/clang/host/linux-x86/clang-r536225
test "$(uname -m)" = x86_64
test "$(git -C "$CAMERA_EXACT/kernel-source" rev-parse HEAD)" = d00ba216ccda5d4fcc0d864729ae69d5b63d860c
cmp "$CAMERA_EXACT/inputs/running.config" "$CAMERA_EXACT/kernel-output/.config"
mkdir -p "$CAMERA_BUILD/kmod/kernel" "$CAMERA_BUILD/kmod/include"
cp "$CAMERA_SOURCE/kernel/"* "$CAMERA_BUILD/kmod/kernel/"
cp "$CAMERA_SOURCE/include/camera_uapi.h" "$CAMERA_SOURCE/include/camera_kernel.h" "$CAMERA_BUILD/kmod/include/"
export PATH="$CAMERA_CLANG/bin:$PATH"
make -C "$CAMERA_EXACT/kernel-source" O="$CAMERA_EXACT/kernel-output" ARCH=arm64 LLVM=1 LLVM_IAS=1 M="$CAMERA_BUILD/kmod/kernel" W=1 modules
