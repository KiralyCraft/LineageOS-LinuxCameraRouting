#!/system/bin/sh
# Only the camera-specific type is changed. Probe the parser offline because
# Magisk 29 strips "0x" and then parses base ten (v29.0 statement.rs).
# Neither the probe type nor an overbroad ioctl rule is loaded into the kernel.
set -eu
CAMERA_POLICY=${CAMERA_POLICY:-/data/adb/magisk/magiskpolicy}
umask 077
CAMERA_PROBE_FILE=$(mktemp /dev/linux-camera-policy.XXXXXX)
trap 'rm -f "$CAMERA_PROBE_FILE"' EXIT
"$CAMERA_POLICY" --save "$CAMERA_PROBE_FILE" \
    'type linux_camera_policy_probe dev_type' \
    'allowxperm untrusted_app linux_camera_policy_probe chr_file ioctl { 0x4b40-0x4b47 }'
CAMERA_PROBE=$(
    "$CAMERA_POLICY" --load "$CAMERA_PROBE_FILE" --print-rules |
        grep '^allowxperm untrusted_app linux_camera_policy_probe chr_file ioctl '
)
case "$CAMERA_PROBE" in
    'allowxperm untrusted_app linux_camera_policy_probe chr_file ioctl { 0x4B40-0x4B47 }')
        CAMERA_CLEAR='~{ 0x0000-0xffff }'
        CAMERA_ALLOW='{ 0x4b40-0x4b47 }'
        ;;
    'allowxperm untrusted_app linux_camera_policy_probe chr_file ioctl { 0x0000 }')
        # Decimal values encoded for the affected parser, not hex values.
        CAMERA_CLEAR='~{ 0x0-0x65535 }'
        CAMERA_ALLOW='{ 0x19264-0x19271 }'
        ;;
    *)
        echo 'Unrecognized ioctl policy parser; broker will not start' >&2
        exit 1
        ;;
esac
"$CAMERA_POLICY" --live \
    "allowxperm untrusted_app linux_camera_device chr_file ioctl $CAMERA_CLEAR" \
    "allowxperm untrusted_app_32 linux_camera_device chr_file ioctl $CAMERA_CLEAR" \
    "allowxperm untrusted_app linux_camera_device chr_file ioctl $CAMERA_ALLOW" \
    "allowxperm untrusted_app_32 linux_camera_device chr_file ioctl $CAMERA_ALLOW"
CAMERA_RULES=$("$CAMERA_POLICY" --print-rules | grep '^allowxperm .* linux_camera_device chr_file ioctl ')
CAMERA_EXPECTED='allowxperm untrusted_app linux_camera_device chr_file ioctl { 0x4B40-0x4B47 }
allowxperm untrusted_app_32 linux_camera_device chr_file ioctl { 0x4B40-0x4B47 }'
if [ "$(printf '%s\n' "$CAMERA_RULES" | sort)" != "$(printf '%s\n' "$CAMERA_EXPECTED" | sort)" ]; then
    # Fail closed even if a parser accepted our input but encoded it wrongly.
    "$CAMERA_POLICY" --live \
        'deny untrusted_app linux_camera_device chr_file ioctl' \
        'deny untrusted_app_32 linux_camera_device chr_file ioctl'
    echo 'Camera ioctl allowlist verification failed; broker will not start' >&2
    exit 1
fi
# Restore the ordinary permission only after validating the extended rules.
"$CAMERA_POLICY" --live \
    'allow untrusted_app linux_camera_device chr_file ioctl' \
    'allow untrusted_app_32 linux_camera_device chr_file ioctl'
echo 'Verified camera ioctl allowlist: 0x4B40-0x4B47'
