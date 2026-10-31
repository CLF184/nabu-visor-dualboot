// SPDX-License-Identifier: MIT
// 「回到 Android」独立 EFI 应用：Renegade 标记程序 + PSCI 复位桥接。
const std = @import("std");
const uefi = std.os.uefi;
const reset = @import("nabu_reset.zig");

pub export var NabuAndroidEntry: *const fn (uefi.Handle, *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status = EfiMain;

pub export fn EfiMain(handle: uefi.Handle, table: *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status {
    uefi.handle = handle;
    uefi.system_table = table;
    reset.toAndroid();
}
