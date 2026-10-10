// Calls the unchanged Renegade Reboot2PayloadApp through a GNU-EFI bridge.
const std = @import("std");
const uefi = std.os.uefi;
extern fn nabu_renegade_android(uefi.Handle, *uefi.tables.SystemTable) callconv(.c) uefi.Status;

pub fn toAndroid() noreturn {
    _ = nabu_renegade_android(uefi.handle, uefi.system_table);
    unreachable;
}
