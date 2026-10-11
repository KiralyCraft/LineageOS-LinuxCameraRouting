#!/system/bin/sh
MODDIR=${0%/*}
CAMERA_RUNTIME=/dev/linux-camera-routing
umask 077
mkdir -p "$CAMERA_RUNTIME"
chmod 0700 "$CAMERA_RUNTIME"
if [ "$(uname -r)" != 5.15.176-gd00ba216ccda ]; then
    echo 'Kernel mismatch; camera module not loaded' > "$CAMERA_RUNTIME/startup.log"
    exit 1
fi
if [ ! -e /sys/module/linux_camera ]; then
    insmod "$MODDIR/kmod/linux_camera.ko" > "$CAMERA_RUNTIME/startup.log" 2>&1 || exit 1
fi
chcon u:object_r:linux_camera_device:s0 /dev/linux_camera || exit 1
# Installation and runtime startup are independent. Install the APK manually;
# a Package Manager failure must never prevent the broker from starting.
while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 1; done
while [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ]; do
    "$MODDIR/bin/linux-camerad" > "$CAMERA_RUNTIME/broker.log" 2>&1 &
    CAMERA_PID=$!
    trap 'kill "$CAMERA_PID" 2>/dev/null; wait "$CAMERA_PID" 2>/dev/null; exit 0' TERM INT
    wait "$CAMERA_PID"
    sleep 1
done
