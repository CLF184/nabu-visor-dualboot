// SPDX-License-Identifier: MIT
// 「回到 Android」独立 EFI 应用（ESP 版本用；固件内驱动版本见 src/menu.zig）。
// 实际动作见 src/nabu_reset.zig：与固件内参考实现一致。
const std = @import("std");
const uefi = std.os.uefi;
const reset = @import("nabu_reset.zig");

pub export var NabuAndroidEntry: *const fn (uefi.Handle, *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status = EfiMain;

pub export fn EfiMain(handle: uefi.Handle, table: *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status {
    _ = handle;
    uefi.system_table = table;
    reset.toAndroid();
}
