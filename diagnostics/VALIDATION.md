# 2026-10-11 validation

Target: Xperia XQ-DQ72, kernel `5.15.176-gd00ba216ccda`.

## Passed

- C99 native and Android Java/APK builds on the designated build server.
- Kernel full-LTO/CFI external-module build using the exact retained kernel tree,
  configuration and symbol versions. No unresolved symbols or module dependencies.
- ASan/UBSan plane-packing test: independent changing patterns, cropped padded
  planes, overlapping interleaved chroma sources, short final rows, bounds checks,
  and no destination mutation on invalid input.
- Live insmod and unload on the phone without touching camera hardware or HDMI.
- Synthetic native lifecycle: producer/consumer DMA-BUF sharing, CPU cache access,
  exact frame contents, buffer retention, no overwrite of application-owned
  frames, exclusive capture ownership, stale generation rejection, cancellation
  during producer ownership, blocked restart before acknowledgment, restart after
  release, and producer death notification.
- `v4l2-compliance` default API checks on the synthetic virtual node:
  48 succeeded, 0 failures, 0 warnings. This is not real-frame streaming validation.

## Requires manually installed APK / live camera qualification

- Helper SELinux FD/mapping permissions in its actual app domain.
- Camera2 physical lens selection and BT.709 raw output.
- Firefox and OBS visual correctness, native orientation and color range.
- Actual 720p/1080p 30/60 and 4K30/60 cadence; metadata alone is insufficient.
- Sensor-to-application latency, copy cost, CPU, frame loss, power and thermal load.
- Screen-off capture, competing Android apps, permissions/privacy changes,
  helper/broker death, and reboot startup.

No real camera image or microphone audio was captured by these synthetic tests.

## Startup correction (0.1.1)

The installed 0.1.0 helper failed before camera enumeration: SELinux denied
`write` on the received `linux_camera_device` descriptor. Its root-created `s0`
label differs from the helper's per-app MLS categories. LineageOS 22.2
`system/sepolicy/private/mls` requires equivalence for character-device writes
unless the subject or object is MLS-trusted. The correction adds only
`linux_camera_device` to `mlstrustedobject`; the helper still has no device-open
permission and the root broker still authenticates the installed package UID.

The Activity also retained its initial “Starting” text after the service had
failed. Version 0.1.1 uses a non-exported, package-scoped status receiver and
retains failure status after shutdown. Native connection failures identify the
failing stage. Startup cleanup is idempotent.

The MLS correction was applied live with SELinux enforcing. The next startup
reached ioctl registration and exposed the separate Magisk parser issue below.

## Ioctl policy correction (0.1.2)

Magisk 29.0's `native/src/sepolicy/statement.rs` strips `0x` and then uses
decimal `parse()`, defaulting to zero on failure. The documented hex allowlist
therefore became `{ 0x0000 }`; real helper registration and stop acknowledgment
were denied for commands 0x4b40 and 0x4b44. This was confirmed by AVCs and
`magiskpolicy --print-rules`, not only inferred from source.

`module/policy.sh` now probes parsing against a temporary policy in Android
tmpfs without loading it, selects the working numeric encoding, replaces only
the two private camera ioctl rules and verifies the loaded allowlist is exactly
0x4B40-0x4B47 before allowing broker startup. It refuses unknown parsing behavior.
The script was run successfully on the phone with SELinux enforcing; its probe
type was not loaded. User confirmed Ready, and seven real metadata-derived
V4L2 nodes registered successfully. No images were captured in this startup test.

Three rear lenses appeared both through the logical camera and as public IDs.
The next APK deduplicates stable identifiers to expose four unique endpoints
instead of overwriting the same three rear aliases. Updated APK testing and
actual camera streaming qualification are still pending.

## Capture-path correction (0.1.3)

The installed 0.1.1 producer was tested at 1280x720@30 against every exposed
node. Three logical-parent physical outputs were rejected during Camera2
session validation. The front camera and three individually public rear IDs
started capture but produced Android dataspace 146931712 (ADATASPACE_JFIF),
which the BT.709-only producer correctly refused to mislabel. Zero frames
reached Linux; no visual-success claim is made for that version.

0.1.3 selects public lens IDs directly and deduplicates stable endpoints.
It requests and validates full-range BT.601-625, with an explicit immutable
color profile in protocol v2 and matching V4L2 metadata. No pixel conversion,
additional image copy, or encoder was added. Old protocol registration is
rejected.

Passed on the phone after live module replacement:
- Both color profiles round-trip through V4L2 G_FMT, TRY_FMT and S_FMT for the
  full-range case, plus G_FMT for BT.709 limited.
- Old protocol, unknown color profile, and duplicate identifier rejection.
- Full existing synthetic buffer-lifecycle and exact-pixel tests.
- V4L2 API compliance: 48/48, zero failures and warnings.

Build and packing sanitizer tests passed on the designated build server.
The new module and broker were loaded temporarily. The user installed the
matching APK; the first real-frame results follow below.

Color references:
- https://developer.android.com/ndk/reference/group/a-data-space
- https://docs.kernel.org/userspace-api/media/v4l/colorspaces-details.html

## First real frames on 0.1.3

All four unique endpoints delivered 1280x720 frames at the requested 30-fps
setting to an ordinary unprivileged Linux FFmpeg V4L2 consumer. Actual cadence
was not measured in this short still-image test. The first 30 frames were
skipped before saving each inspected PNG, with explicit full-range BT.601
conversion for viewing. Images and logs remain only in RAM-backed temporary
storage; no camera images are committed.

- Front: clearly visible room detail, visual pass.
- Rear 2.68 mm (ultrawide): visible scene detail, visual pass.
- Rear 6.08 mm (main): dark, blurred frame; delivery passed, scene verification pending.
- Rear 10.22 mm (telephoto): almost black frame; delivery passed, scene verification pending.

The user was asked to point the rear lenses at an unobstructed lit scene before
retesting the latter two. Nonzero image bytes alone do not establish that a
camera is showing the intended scene. No capture or dataspace error occurred.
V4L2 G_FMT reports 470 System BG primaries, Rec.709 transfer, ITU-R 601 matrix
and full quantization. Android reports no active camera clients after the test;
the helper returned to Ready.

## Main/telephoto retry after repositioning

After the user flipped the phone, both rear-2 (main) and rear-3 (telephoto)
produced non-black 1280x720 images, visually inspected from the Linux V4L2
consumer. The main image shows the ceiling/light fixture; the telephoto image
shows a narrower ceiling view with lighting variation. The main image is soft;
this is a basic capture/non-black check, not autofocus or image-quality
qualification. No code or exposure setting was changed between the dark first
pass and this retry.

Main grayscale range was 7..255, mean 110.876; telephoto 65..255, mean 140.742.
These are statistics of the inspection PNGs, not sensor exposure measurements.
Together with the first front/ultrawide pass, all four endpoints now have a
visually inspected non-black capture. No active Android camera clients remained
after cleanup, and the helper returned to Ready. Private images remain in RAM
only; no images were added to the repository.
