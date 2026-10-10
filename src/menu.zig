// SPDX-License-Identifier: MIT
// 固件卷内 DXE 驱动（PE subsystem 11）：在原固件 BDS 入口之前显示 Visor 菜单。
//
// 与首次移植（失败版本）的关键差别，也是本版修掉的两个黑屏原因：
//   - 选 Windows：不自行链式加载镜像，而是从 Visor 干净返回后调用固件
//     **原始 BDS 入口**，让固件用它自己的完整流程启动 Windows（与未修改固件一致）。
//   - 选 Android：直接写 DRAM 标记 + Stall(0.5s) + 热重启（照抄固件内参考实现），
//     不依赖 ESP 上的任何程序，也不受安全启动签名限制。
const std = @import("std");
const uefi = std.os.uefi;
const Input = uefi.protocol.SimpleTextInputEx;
const reset = @import("nabu_reset.zig");

const Bds = extern struct { entry: *const fn (*Bds) callconv(uefi.cc) void };
const bds_guid align(8) = uefi.Guid{
    .time_low = 0x665e3ff6, .time_mid = 0x46cc, .time_high_and_version = 0x11d4,
    .clock_seq_high_and_reserved = 0x9a, .clock_seq_low = 0x38,
    .node = .{ 0, 0x90, 0x27, 0x3f, 0xc1, 0x4d },
};

var saved_entry: *const fn (*Bds) callconv(uefi.cc) void = undefined;
// 重定位锚点：部分 PE 加载器不接受空重定位表。
pub export var NabuBdsHook: *const fn (*Bds) callconv(uefi.cc) void = bdsEntry;
var shown = false;

extern fn nabu_visor_menu(handle: uefi.Handle, table: *uefi.tables.SystemTable) callconv(.c) void;
// 由 Visor 主流程写入（1 = 交回固件 BDS / Windows，2 = 标记热重启 / Android）
extern var nabu_exit_action: c_int;

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

fn protocol(comptime T: type, guid: *align(8) const uefi.Guid) ?*T {
    var ptr: ?*anyopaque = null;
    if (uefi.system_table.boot_services.?.locateProtocol(guid, null, &ptr) != .success) return null;
    return if (ptr) |p| @ptrCast(@alignCast(p)) else null;
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
    // 必须无条件先连接所有控制器：BDS 之前显示（GOP）尚未绑定，
    // 不连接就会出现"没有 GOP → Visor 无法初始化 → 菜单不出现"。
    // 真机教训：这里保持与已验证可用的首版完全一致，不做任何日志/额外操作。
    connectAll();
    _ = collectInputs();
    nabu_visor_menu(uefi.handle, uefi.system_table);
}

fn bdsEntry(this: *Bds) callconv(uefi.cc) void {
    if (!shown) {
        shown = true;
        menu();
        if (nabu_exit_action == 2) {
            // Android：写标记 + 热重启（不返回）
            reset.toAndroid();
        }
        // 1 = Windows：交回固件原始 BDS；0 = Visor 异常退出：同样回退到固件正常流程。
    }
    saved_entry(this);
}

pub export fn EfiMain(handle: uefi.Handle, table: *uefi.tables.SystemTable) callconv(uefi.cc) uefi.Status {
    uefi.handle = handle;
    uefi.system_table = table;
    const bds = protocol(Bds, &bds_guid) orelse return .not_found;
    saved_entry = bds.entry;
    bds.entry = NabuBdsHook;
    return .success;
}
