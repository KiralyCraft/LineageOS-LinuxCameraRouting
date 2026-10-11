# Live packer comparison, 2026-10-11

Tested APK 0.1.4 from ba4843c, installed module/broker 0.1.3; kernel unchanged.
Phone: Xperia XQ-DQ72, externally powered and dozing throughout, battery 100%,
31.0 C during/end of measurements. No CPU affinity, governor or thermal changes.
This differs from the awake 0.1.3 baseline; comparisons below use only the same
0.1.4 APK under matching conditions, switching its private diagnostic control.

## Method and results

Main camera, reference → fast → reference for each mode. Unprivileged FFmpeg
V4L2 input, raw stream copy to /dev/null: no encoder, decoder, image storage or
color conversion in the timing runs. Three seconds after launch for startup,
then 15 seconds of /proc CPU and FFmpeg progress sampling. Stage counters cover
the entire stream, including startup. Continuous filtered logcat captured all
retirement summaries in RAM-backed /tmp. SIGINT retirement was clean each time.
Full stage counts, means, maxima, histogram bounds and logs are in
`packing-live-20261011.json`.

Reference columns average the two bracketing run means; CPU 100% means one core.
FPS derives from delivered frame count and timestamp interval, not display FPS
or a proof that every sensor frame reached the application.

| Mode | Reference pack ms | Fast pack ms | Reference helper CPU | Fast helper CPU | Fast timestamp fps |
|---|---:|---:|---:|---:|---:|
| 1920×1080 @30 | 7.41 | 1.08 | 25.76% | 10.13% | 29.981 |
| 1920×1080 @60 | 5.33 | 1.10 | 36.69% | 15.73% | 59.947 |
| 3840×2160 @30 | 17.26 | 4.92 | 49.45% | 17.93% | 29.981 |

Reference pack costs returned after selecting reference again: 7.27/7.55 ms
at 1080p30, 5.48/5.17 ms at 1080p60, 17.28/17.25 ms at 4K30. Mean fast packing
is 3.5–6.8× faster; helper CPU is 57–64% lower. FFmpeg used 1.4–2.0% of one core,
and broker CPU rounded to 0.00%. Sony's provider remains a separate, substantial
cost (roughly 93–133% here); this optimization does not remove HAL/ISP work.

## Where the time goes

Across all nine runs, combined explicit destination DMA_BUF_IOCTL_SYNC start/end
means were 0.015–0.024 ms/frame. Source plane access averaged 0.136–0.239 ms;
it can include gralloc locking/cache work. Acquisition averaged 0.032–0.050 ms.
The measured dominant bridge stage was packing, not these explicit sync calls.
Packing still includes actual memory/cache behavior and scheduling; these wall
times do not distinguish arithmetic from every memory stall. Both sync ioctls,
source-image lifetime and kernel buffer-retirement semantics remain intact.

All measured sources used Y stride equal to width, U/V row strides equal to
width, and pixel strides 1/2/2. The optimized interleaved-chroma path is
applicable; output remains planar I420 with identical sample values.

Fast pack maximum was 3.73 ms at 1080p30, 5.75 ms at 1080p60, 20.67 ms at 4K30;
reference 4K maxima were 41.91/41.76 ms. Histogram p95 is a coarse upper bound,
not an exact percentile. All fast matrix runs reported zero no-destination-buffer
drops except one at 1080p60; the reference runs reported five in total. These
counters exclude frames discarded by acquireLatestImage. One reference 1080p60
interval delivered 59.880 fps versus 59.947 in the other reference and fast runs.
No claim of lossless sensor cadence or measured end-to-end latency is made.

## Image and lifecycle checks

After the matrix, selected fast mode and captured a 720p frame after skipping
30 frames from each of main, telephoto, ultrawide and front. All four displayed
recognizable non-black scene detail with no visible packing/plane artifacts.
Main-camera focus remains soft, also observed before this optimization; this
is not an autofocus qualification. Images stay only in RAM-backed /tmp and are
not committed. Algorithmic pixel identity is covered separately by the 224
independent pattern/reference/guard-page cases in `PACKING-20261011.md`.

All stream stops returned to Ready, including transitions across dimensions,
frame rates and lenses. Restored all node formats to 1080p30. No camera client
remains active; helper remains enabled, with fast mode selected for this service
lifetime. APK 0.1.4 resets to reference on service restart.

## Promotion

0.1.5 changes the initial packer selection to fast; the measured algorithm and
synchronization path are unchanged. Reference remains available as a private
idle-only diagnostic override. The new APK requires manual installation and
Enable Linux cameras. Existing 0.1.3 Magisk module remains compatible; no module
installation or reboot is required. The 0.1.5 binary is build-validated separately;
the live comparisons above were performed with manually installed 0.1.4.
