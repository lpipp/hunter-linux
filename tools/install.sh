#!/bin/sh
# Install the read-only version, replacing the previous backend if present.
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
[ "$(id -u)" -eq 0 ] || { printf '%s\n' 'Administrator authorization required' >&2; exit 1; }
root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd)
kernel=$(uname -r)
[ "$kernel" = 7.0.0-38-generic ] || { printf '%s\n' 'This installer targets 7.0.0-38-generic' >&2; exit 1; }
[ "$(cat /sys/class/dmi/id/product_name)" = FRD-WX9 ]
[ "$(cat /sys/class/dmi/id/board_name)" = FRD-WX9-PCB ]
[ "$(cat /sys/class/dmi/id/bios_version)" = 1.18 ]
[ "$(modinfo -F version "$root/kernel/huawei-wmi.ko")" = hunter-0.3.0 ]
case $(modinfo -F vermagic "$root/kernel/huawei-wmi.ko") in
    "$kernel "*) ;;
    *) printf '%s\n' 'Kernel module mismatch' >&2; exit 1 ;;
esac

module=/lib/modules/$kernel/updates/hunter/huawei-wmi.ko
backup=/var/lib/hunter-readonly/backups/$(date -u +%Y%m%dT%H%M%SZ)
umask 077
mkdir -p "$backup"
printf '%s\n' "$kernel" > "$backup/kernel"
cat /sys/bus/platform/devices/huawei-wmi/fn_lock_state > "$backup/fn-lock"
cat /sys/bus/platform/devices/huawei-wmi/charge_control_thresholds > "$backup/charge-thresholds"
systemctl is-enabled hunter-platform.service > "$backup/service-enabled" 2>/dev/null || :
systemctl is-active hunter-platform.service > "$backup/service-active" 2>/dev/null || :
sha256sum "$root/kernel/huawei-wmi.ko" "$root/bin/hunter-mode" "$root/bin/hunter-fans" > "$backup/source-sha256"
set --
for name in battery_reset report_brightness; do
    value=$(cat "/sys/module/huawei_wmi/parameters/$name")
    case $value in 0|1) set -- "$@" "$name=$value" ;; esac
done

# Save both removed files and replaced files before stopping the old service.
cat > "$backup/targets" <<EOF
$module
/boot/initrd.img-$kernel
/usr/bin/hunter-mode
/usr/bin/hunter-fans
/usr/bin/huntet-fans
/usr/bin/hunterctl
/usr/bin/hunterd
/usr/lib/hunter-platform
/usr/lib/systemd/system/hunter-platform.service
/usr/share/dbus-1/system.d/org.hunter.Platform1.conf
/usr/share/dbus-1/system-services/org.hunter.Platform1.service
/usr/share/polkit-1/actions/org.hunter.platform.policy
/usr/share/hunter-platform
/etc/modprobe.d/hunter-platform.conf
/etc/modules-load.d/hunter-platform.conf
EOF
: > "$backup/existing"
: > "$backup/absent"
while IFS= read -r path; do
    if [ -e "$path" ] || [ -L "$path" ]; then
        printf '%s\n' "${path#/}" >> "$backup/existing"
    else
        printf '%s\n' "$path" >> "$backup/absent"
    fi
done < "$backup/targets"
tar -cpf "$backup/files.tar" -C / -T "$backup/existing"

rollback() {
    status=$1
    shift
    trap - 0 HUP INT TERM
    set +e
    printf 'Installation failed; restoring %s\n' "$backup" >&2
    umask 022
    modprobe -r huawei_wmi
    while IFS= read -r path; do rm -rf -- "$path"; done < "$backup/absent"
    tar -xpf "$backup/files.tar" -C /
    depmod -a "$kernel"
    modprobe huawei_wmi "$@"
    systemctl daemon-reload
    systemctl reload dbus.service
    if grep -qx enabled "$backup/service-enabled"; then systemctl enable hunter-platform.service; fi
    if grep -qx active "$backup/service-active"; then systemctl start hunter-platform.service; fi
    exit "$status"
}
trap 'rollback $? "$@"' 0
trap 'exit 1' HUP INT TERM
umask 022

printf '%s\n' 'Removing the previous service and authorization files'
if [ -f /usr/lib/systemd/system/hunter-platform.service ]; then
    systemctl disable --now hunter-platform.service
fi
modprobe -r huawei_wmi
rm -f /usr/bin/huntet-fans /usr/bin/hunterctl /usr/bin/hunterd /usr/lib/systemd/system/hunter-platform.service \
    /usr/share/dbus-1/system.d/org.hunter.Platform1.conf \
    /usr/share/dbus-1/system-services/org.hunter.Platform1.service \
    /usr/share/polkit-1/actions/org.hunter.platform.policy /etc/modprobe.d/hunter-platform.conf
rm -rf /usr/lib/hunter-platform /usr/share/hunter-platform
install -m 755 "$root/bin/hunter-mode" "$root/bin/hunter-fans" /usr/bin/
mkdir -p "$(dirname "$module")"
install -m 644 "$root/kernel/huawei-wmi.ko" "$module"
printf '%s\n' huawei_wmi > /etc/modules-load.d/hunter-platform.conf
chmod 644 /etc/modules-load.d/hunter-platform.conf
depmod -a "$kernel"
[ "$(modinfo -n huawei_wmi)" = "$module" ]
update-initramfs -u -k "$kernel"
modprobe huawei_wmi "$@"
udevadm settle --timeout=10
systemctl daemon-reload
systemctl reload dbus.service
printf '%s\n' 'Verifying read-only queries'
mode=$(/usr/bin/hunter-mode)
fans=$(/usr/bin/hunter-fans)
[ "$(cat /sys/bus/platform/devices/huawei-wmi/fn_lock_state)" = "$(cat "$backup/fn-lock")" ]
[ "$(cat /sys/bus/platform/devices/huawei-wmi/charge_control_thresholds)" = "$(cat "$backup/charge-thresholds")" ]
[ ! -e /dev/hunter-platform ]
printf 'hunter-mode: %s\nhunter-fans: %s\nbackup: %s\n' "$mode" "$fans" "$backup"
printf '%s\n' installed > "$backup/result"
trap - 0 HUP INT TERM
