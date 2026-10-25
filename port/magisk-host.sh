#!/bin/sh
# Host I/O adapter only; the selected APK's patch script remains unchanged.
BOOTMODE=false
SOURCEDMODE=true
. ./util_functions.sh
ui_print() { printf '%s\n' "$*"; }
abort() { ui_print "$*"; exit 1; }
export BOOTMODE SOURCEDMODE
export KEEPVERITY=true KEEPFORCEENCRYPT=true PATCHVBMETAFLAG=false
export RECOVERYMODE=false LEGACYSAR=false CHROMEOS=false
# nabu's existing Magisk configuration uses the metadata pre-init partition.
PREINITDEVICE=${PREINITDEVICE:-metadata}
export PREINITDEVICE
. ./boot_patch.sh input.img
