// SPDX-License-Identifier: MIT
// nabu_visor_dualboot — ESP 标准 EFI 应用入口（PE subsystem 10）。
// 固件的 DXE/BDS/固件卷保持原样；本程序只作为 ESP 上的启动器被固件加载，
// 之后按上游 Visor 的行为显示菜单并链式加载所选系统。
const std = @import("std");
const uefi = std.os.uefi;
const Input = uefi.protocol.SimpleTextInputEx;

extern fn nabu_visor_menu(handle: uefi.Handle, table: *uefi.tables.SystemTable) callconv(.c) void;

// 重定位锚点：部分 PE 加载器不接受空重定位表。
pub export var NabuVisorEntry: *const fn (uefi.Handle, *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status = EfiMain;

var visor_inputs: [16]*Input = undefined;
var visor_input_count: usize = 0;
var visor_translator = @import("visor_input.zig").Translator{};
const VisorKey = @import("visor_input.zig").Key;

pub export fn NabuReadKey(key: *VisorKey) callconv(.c) uefi.Status {
    var scan: u16 = 0;
    for (visor_inputs[0..visor_input_count]) |input| {
        var k: Input.Key = std.mem.zeroes(Input.Key);
        if (input.readKeyStrokeEx(&k) == .success) {
            // 保留 USB 键盘文本、功能键与快捷键给上游编辑器、文件浏览与恢复控制台。
            if (k.input.unicode_char != 0 or (k.input.scan_code != 0 and k.input.scan_code != 1 and k.input.scan_code != 2 and k.input.scan_code != 0x102)) {
                key.* = .{ .scan = k.input.scan_code, .unicode = k.input.unicode_char };
                return .success;
            }
            const code: u16 = k.input.scan_code;
            if (code != 0) scan = code;
            _ = input.reset(false);
        }
    }
    return if (visor_translator.translate(scan, key)) .success else .not_ready;
}

fn connectAll() void {
    const bs = uefi.system_table.boot_services.?;
    // 每轮重新枚举：连接总线可能产生新的控制器句柄。
    for (0..3) |_| {
        var count: usize = 0;
        var handles: [*]uefi.Handle = undefined;
        if (bs.locateHandleBuffer(.all_handles, null, null, &count, &handles) != .success) return;
        for (handles[0..count]) |h| _ = bs.connectController(h, null, null, true);
        _ = bs.freePool(@ptrCast(@alignCast(handles)));
    }
}

fn collectInputs() bool {
    const bs = uefi.system_table.boot_services.?;
    var count: usize = 0;
    var handles: [*]uefi.Handle = undefined;
    if (bs.locateHandleBuffer(.by_protocol, &Input.guid, null, &count, &handles) != .success) return false;
    defer _ = bs.freePool(@ptrCast(@alignCast(handles)));
    for (handles[0..count]) |h| {
        var p: ?*anyopaque = null;
        if (visor_input_count < visor_inputs.len and bs.handleProtocol(h, &Input.guid, &p) == .success and p != null) {
            visor_inputs[visor_input_count] = @ptrCast(@alignCast(p.?));
            visor_input_count += 1;
        }
    }
    return visor_input_count != 0;
}

fn menu() void {
    // 固件在启动本程序前已经连接过控制器；找不到输入协议时才补做一次连接。
    if (!collectInputs()) {
        connectAll();
        _ = collectInputs();
    }
    nabu_visor_menu(uefi.handle, uefi.system_table);
}

pub export fn EfiMain(handle: uefi.Handle, table: *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status {
    uefi.handle = handle;
    uefi.system_table = table;
    menu();
    return .success;
}
