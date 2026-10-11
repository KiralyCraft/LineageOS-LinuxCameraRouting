# Linux Camera Routing

Android Camera2 capture exposed to the same device's Linux chroot as ordinary
V4L2 webcams. Source: https://github.com/KiralyCraft/LineageOS-LinuxCameraRouting.

## Current release: 0.1.2, first device-test candidate

The kernel module's synthetic lifecycle tests and 48 V4L2 API compliance tests
have passed on the Xperia XQ-DQ72. Helper startup and camera registration now pass with SELinux enforcing.
Actual camera capture, physical-lens selection, application interoperability,
screen-off capture and high-rate performance remain to be qualified. Supported-mode metadata is not a measured
frame-rate result. See `diagnostics/VALIDATION.md` for the exact tested boundary.

## Architecture

Camera2 -> native YUV image reader -> one copy/plane packing operation ->
V4L2 capture buffer shared with the Linux application through mmap.

No JPEG/H.264 encoder or decoder, RGB intermediate, video socket payload, Linux
frame relay, scaling, rotation, or mirroring. Android's camera HAL/ISP produces
the YUV image; the bridge preserves its pixel samples. It requests BT.709 limited
range and rejects an unexpected dataspace instead of advertising incorrect colors.

The camera broker authenticates the installed `dev.kiraly.linuxcamera` UID,
hands over the exclusive kernel producer descriptor, and grants UID/GID 4000
access only to new `linux-camera` nodes. The broker never receives frame data.
Each capture allocation is exported and mapped once per generation.

The helper exposes front and individually selectable rear physical endpoints
where Camera2 permits them. V1 conservatively admits one hardware stream across
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

1. Install `LinuxCameraRouting-0.1.2.zip` through Magisk, then reboot. It is pinned
   to kernel `5.15.176-gd00ba216ccda`; a mismatched kernel is refused.
2. Install `LinuxCamera-0.1.2.apk` manually. Grant camera permission, open the app,
   and tap **Enable Linux cameras**.
3. In Linux, optionally run `./install-linux.sh` to install `linux-cameractl`.
   Select a Linux Camera entry in Firefox, OBS or another V4L2 application.

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
