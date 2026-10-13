# Camera protocol v2

`include/camera_uapi.h` defines fixed-width kernel structures. This first release
targets the verified little-endian AArch64 Android/chroot pair. The registration
contains an explicit version and structure size; reserved input fields must be
zero. Capability flags unsupported by v2 are not silently accepted.

The root-owned abstract Unix SOCK_SEQPACKET listener is
`linux-camera-broker-v2`. It validates SO_PEERCRED against the exact package UID
in `/data/system/packages.list`. Only that helper receives an SCM_RIGHTS control
FD with a one-byte `F` marker. The helper verifies the root peer and descriptor
message. The broker closes its own FD immediately after transfer. No app UID is
cached across APK reinstall or package UID changes.

The helper registers each endpoint through CAMERA_REGISTER, then sends its
returned structure to the broker. The broker validates the actual device with
VIDIOC_QUERYCAP before setting access and publishing an alias. A one-byte `K`
acknowledges publication; `E` closes the failed connection. No frame bytes use
this socket. HUP is a lifetime event and causes helper shutdown.

## Stream ownership

- CAMERA_EVENTS snapshots event readiness before a complete CAMERA_QUERY scan.
  Updates during the scan remain visible to poll; QUERY itself does not consume
  events. Event generation does not replace capture cadence or frame timestamps.
- STREAMON reserves the hardware slot and creates a new generation. The helper
  starts Camera2 only for that generation.
- CAMERA_ACQUIRE assigns one queued destination and a monotonically increasing
  token. CAMERA_EXPORT returns its allocation FD; duplicate exports are not
  needed in steady state. DMA_BUF_IOCTL_SYNC surrounds CPU writes.
- CAMERA_COMPLETE validates camera, generation, token, byte count and timestamp
  before vb2_buffer_done publishes the frame. The application owns it from DQBUF
  until QBUF. No producer reuse is allowed in that interval.
- STREAMOFF returns queued buffers with an error, increments the generation and
  marks the endpoint retiring. New STREAMON requests fail while retiring.
- The helper closes the Camera2 session/device, drains image callbacks, removes
  producer mappings/FDs, then sends CAMERA_ACK_STOP for the stopped generation.
  Only then can the hardware slot and pool be reused.
- CAMERA_FAIL errors the queue and initiates the same retirement. Closing the
  sole producer control FD disconnects/unregisters nodes and wakes consumers.

Exported DMA-BUFs and mmap VMAs pin memory even when the queue is destroyed.
Stale completion ioctls cannot reference a replacement queue. Acknowledgment is
a contract with the authenticated producer: it must never acknowledge retirement
while retaining code that can write the old mappings.

Native source acquisition establishes camera readiness before reading planes.
No unfinished GPU operation is approximated with a timer. The only timeout in
node setup bounds asynchronous ueventd provisioning. Sensor BOOTTIME timestamps
are converted to CLOCK_MONOTONIC using a freshly sampled offset per frame.

Native callbacks serialize access to the single active reader. Stop first detaches
it, then deletes it without holding the callback mutex, and finally closes shared
mappings. Camera2 callbacks are generation-checked on one Java handler thread.

## Immutable color contract

Protocol v2 replaces the registration reserved word with a required color
profile. Structure sizes and ioctl command numbers are unchanged, but v1
registration is rejected. Duplicate endpoint identifiers are also rejected.

- BT709_LIMITED: Rec.709 primaries and matrix, limited quantization, Rec.709 transfer.
- BT601_625_FULL: BT.601-625 primaries (V4L2 470_SYSTEM_BG), BT.601 matrix,
  full quantization, SMPTE 170M transfer (identical to Rec.709 transfer).

The Android producer registers BT601_625_FULL, requests ADATASPACE_JFIF and
checks every acquired image against it before copying. G_FMT, TRY_FMT and
S_FMT expose the registered profile before streaming. A mismatched dataspace
errors the stream; metadata never changes silently midway through capture.
This is raw planar YUV, not a JPEG codec despite Android's JFIF dataspace name.

## V4L2 retirement barrier (module 0.1.6)

A successful STREAMOFF waits for CAMERA_ACK_STOP of the stopped generation;
final consumer close also waits when it caused or observes pending retirement.
The standard V4L2/vb2 operation runs first, then the wait runs outside the queue
mutex, so the helper can process QUERY/ACK_STOP. The V4L2 open reference keeps
the video device alive during both waits. Camera2 onClosed, image-reader deletion
and mapping release still precede acknowledgment in the existing helper.

ACK_STOP releases hardware admission before clearing the wait condition and
waking callers. Producer disconnect likewise releases admission and wakes stop
waiters, while marking the queue disconnected/error. Stop notification itself
is still asynchronous to the helper; the consumer sees completed retirement.
There is no periodic polling or per-frame wait. An interrupted wait returns its
signal error; a five-second fault deadline returns ETIMEDOUT and logs a warning.
Neither path fakes acknowledgment or clears the retirement/admission state.
A dead or faulty producer therefore cannot authorize early buffer reuse. Final
close errors are ignored by V4L2 core, but the retirement remains blocked until
real acknowledgment or producer disconnect.
