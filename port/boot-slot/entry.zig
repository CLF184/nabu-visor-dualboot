// SPDX-License-Identifier: MIT
// Initialize only the EDK2 library globals, then execute upstream BootSlotMain.
const std = @import("std");
const uefi = std.os.uefi;
extern fn NabuBootSlotInitialize(*uefi.tables.SystemTable) callconv(.c) uefi.Status;
extern fn BootSlotMain(uefi.Handle, *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status;
pub export var NabuBootSlotEntry: *const fn (uefi.Handle, *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status = EfiMain;
pub export fn EfiMain(handle: uefi.Handle, table: *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status {
    const status = NabuBootSlotInitialize(table);
    if (status != .success) return status;
    return BootSlotMain(handle, table);
}
