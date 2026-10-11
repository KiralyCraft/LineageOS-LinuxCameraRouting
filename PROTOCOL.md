# Camera protocol v1

`include/camera_uapi.h` defines fixed-width kernel structures. This first release
targets the verified little-endian AArch64 Android/chroot pair. The registration
contains an explicit version and structure size; reserved input fields must be
zero. Capability flags unsupported by v1 are not silently accepted.

The root-owned abstract Unix SOCK_SEQPACKET listener is
`linux-camera-broker-v1`. It validates SO_PEERCRED against the exact package UID
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
