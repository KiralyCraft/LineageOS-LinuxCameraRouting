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
