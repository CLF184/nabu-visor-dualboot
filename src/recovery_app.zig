// SPDX-License-Identifier: MIT
// 独立 Recovery EFI 应用；只链接 BCB、标记与 GNU-EFI，不链接菜单入口。
const std = @import("std");
const uefi = std.os.uefi;
extern fn InitializeLib(uefi.Handle, *uefi.tables.SystemTable) callconv(.c) void;
extern fn nabu_recovery_boot() callconv(.c) uefi.Status;

pub export var NabuRecoveryEntry: *const fn (uefi.Handle, *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status = EfiMain;

pub export fn EfiMain(handle: uefi.Handle, table: *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status {
    InitializeLib(handle, table);
    return nabu_recovery_boot();
}
