// SPDX-License-Identifier: MIT
// 「回到 Android」：与固件内参考实现（vol-button FD 中的应用）逐步骤一致——
//   1. 把一次性标记 0xC0DEADDED 写到保留 DRAM 0x17FE00000
//   2. 缓存写回 + 屏障
//   3. Stall 0.5 秒（参考实现如此）
//   4. ResetSystem(EfiResetWarm)
// 内核入口分流代码（官方 ShellCode.Nabu2）读到标记后跳回原 Android 内核入口。
const std = @import("std");
const uefi = std.os.uefi;

pub const MARKER_ADDR: usize = 0x17fe00000;
pub const MARKER_VALUE: u64 = 0xc0deadded;

pub fn toAndroid() noreturn {
    const marker: *volatile u64 = @ptrFromInt(MARKER_ADDR);
    marker.* = MARKER_VALUE;
    // 关键：把标记从缓存写回到 DRAM。热重启会丢弃缓存内容，
    // 没有这一步，分流代码在重启后读不到标记（真机实测：少了它就会回到菜单）。
    asm volatile ("dsb sy" ::: "memory");
    asm volatile ("dc cvac, %[p]" :: [p] "r" (@intFromPtr(marker)) : "memory");
    asm volatile ("dsb sy" ::: "memory");
    // 再等 0.5 秒后热重启（与固件内参考实现的时序一致）。
    const st = uefi.system_table;
    _ = st.boot_services.?.stall(500_000);
    st.runtime_services.resetSystem(.reset_warm, .success, 0, null);
}
