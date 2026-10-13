# Linux Camera Routing

Android Camera2 capture exposed to the same device's Linux chroot as ordinary
V4L2 webcams. Source: https://github.com/KiralyCraft/LineageOS-LinuxCameraRouting.

## Current release: 0.1.6, acknowledged camera retirement

Version 0.1.6 fixes a reproduced Firefox resolution-change race in the kernel
module: STREAMOFF/final close now wait for the helper's retirement acknowledgment
before returning. The queue lock is released while waiting. The installed 0.1.5
APK remains compatible and needs no update for this fix. Install the new module
manually and reboot, then enable the helper. Isolated Firefox front/rear
640x480-to-1080p capture and the expanded lifecycle tests pass on the installed
module. See `diagnostics/FIREFOX-20261013.md` for the reproduction and limits.

The Xperia XQ-DQ72 passes synthetic buffer-lifecycle tests and 48 V4L2 API
compliance tests with SELinux enforcing. All four lenses produced visually
verified 720p frames. Main-camera stream-copy measurements cover 1080p30,
1080p60 and 4K30, including screen-dozing operation. The optimized packer reduces
helper CPU use by approximately 57–64% in matched live comparisons. This is not
qualification of every advertised mode, autofocus, or Firefox/OBS integration.
See `diagnostics/VALIDATION.md` and `diagnostics/PACKING-LIVE-20261011.md`.

## Architecture

Camera2 -> native YUV image reader -> one copy/plane packing operation ->
V4L2 capture buffer shared with the Linux application through mmap.

No JPEG/H.264 encoder or decoder, RGB intermediate, video socket payload, Linux
frame relay, scaling, rotation, or mirroring. Android's camera HAL/ISP produces
the YUV image; the bridge preserves its pixel samples. It requests full-range BT.601-625 (Android JFIF) and verifies that dataspace
on every image. V4L2 advertises matching primaries, transfer, matrix and range;
unexpected dataspaces fail instead of silently relabeling or converting samples.

The camera broker authenticates the installed `dev.kiraly.linuxcamera` UID,
hands over the exclusive kernel producer descriptor, and grants UID/GID 4000
access only to new `linux-camera` nodes. The broker never receives frame data.
Each capture allocation is exported and mapped once per generation.

The helper exposes front and individually selectable rear physical endpoints
where Camera2 permits them. It prefers publicly openable lens IDs; physical
output routing through a logical parent is used only for hidden lens IDs. V1 conservatively admits one hardware stream across
all endpoints, and one capture owner per node. Camera enumeration does not open
hardware. Linux STREAMON starts it; STREAMOFF/close releases it. The app's
foreground service and Android camera indicator remain visible while enabled.

Modes are selected from 640x480, 1280x720, 1920x1080 and 3840x2160 at fixed
15/24/30/60 fps where reported by Android. The default is 1080p30 when supported.
Physical session support is checked when opening. Rejected combinations fail
explicitly and are logged; their inclusion in initial enumeration is provisional
until real-camera qualification. Native sensor orientation is retained.

There is no frame backlog in the bridge. The image reader acquires the latest
frame, and frames arriving without a queued Linux buffer are discarded and
counted. Such frame drops differ from compression or changing pixel samples.

## Installation and startup

1. Install `LinuxCameraRouting-0.1.6.zip` through Magisk, then reboot. It is pinned
   to kernel `5.15.176-gd00ba216ccda`; a mismatched kernel is refused.
2. Install `LinuxCamera-0.1.6.apk` manually. Grant camera permission, open the app,
   and tap **Enable Linux cameras**.
3. In Linux, optionally run `./install-linux.sh` to install `linux-cameractl`.
   Select a Linux Camera entry in Firefox, OBS or another V4L2 application.

Version 0.1.3 and later use protocol v2. APK 0.1.5 works with module 0.1.6;
updating the APK alone does not fix the kernel retirement race.
The first color fix requires a matched APK, broker and kernel module;
older producers are rejected rather than interpreted with the wrong colors.
The module boots the broker independently of APK installation. The helper must
be enabled from Android after a reboot. Enabling it registers devices but leaves
camera hardware idle until a Linux application streams. No LXDE audio changes
or additional Linux daemon are needed. This works independently of HDMI.

For live development, load the exact `.ko` and start the native broker from
Android root after applying the supplied SELinux policy and labeling the private
control node, then run `module/policy.sh` from Android root before the broker.
The script probes the installed Magisk parser offline and verifies the exact
camera ioctl allowlist; it handles Magisk 29's hexadecimal parsing defect.
The packaged Magisk service encodes the exact setup. Do not unload
while the helper, consumers, or exported mappings are alive. Stop the helper and
close camera applications first. Reboot clears a temporary insmod deployment.

## Inspection

`linux-cameractl list`, `status`, and `diagnostics` inspect the virtual camera
nodes without starting capture. Stable aliases appear under `/dev/linux-camera/`.
`v4l2-ctl -d /dev/linux-camera/front-1 --list-formats-ext` lists reported modes.
Native frame/copy/drop counters appear in Android logcat under `LinuxCamera` on
stream retirement; the app reports camera and mode failures.

Runtime broker logs are bounded to the current process lifetime in
`/dev/linux-camera-routing/`. Development reports and captures belong in tmpfs.
The software does not save images by default.

## Reproducible build

Build only on `root@192.168.104.201`:

```sh
bash build-support/build.sh SOURCE BUILD
python3 build-support/package.py SOURCE BUILD OUTPUT
```

The scripts use the retained exact kernel source, effective config and
Module.symvers from `/bigdata/hdmi-los-build/work/companion-probe-20261005` and
Android Clang r536225 (19.0.1), preserving CFI, full LTO and symbol versioning.
Android userspace uses NDK 29 and API 35; Java is only the UI/service/Camera2
control layer. The frame path, broker, and kernel module are C.

Kernel module code is GPL-2.0-only. The kernel's public APIs and exported memory
operations are reused; no Qualcomm camera module is replaced or intercepted.

## Packing and diagnostics

Version 0.1.5 starts with the validated fast packer (0.1.4 defaulted to the
reference implementation). It retains per-stream stage
summaries (mean, histogram p95 upper bound, maximum) for acquisition, destination
mapping, source access, DMA-BUF sync start, packing, sync end, completion and
source release. Only stream-stop summaries are logged, never per-frame output.
The counters reset per stream. Source access may itself include HAL cache work;
source image acquisition may wait for its readiness fence.

The candidate packs adjacent interleaved chroma planes with AArch64 NEON in one
pass, handles the exact scalar tail and retains the generic fallback. Planar
contiguous rows use memcpy without changing samples. All source bounds are
validated before any destination write. The protocol, pixel values, layout,
color interpretation, buffer ownership and fences remain unchanged.

For controlled testing, Android root can switch the existing idle helper:

```sh
am startservice -n dev.kiraly.linuxcamera/.CameraService --es packing_mode fast
am startservice -n dev.kiraly.linuxcamera/.CameraService --es packing_mode reference
```

This is a private diagnostic service control, not an exported app interface.
The helper must already be enabled; changes during capture are rejected. Mode
selection lasts until the helper stops. Confirm `Packing mode:` in logcat before
starting the next capture. The installed 0.1.3 module needs no update for this APK.
