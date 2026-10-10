// SPDX-License-Identifier: MIT
const std = @import("std");
pub const Action = enum { none, changed, confirm, boot_android };
pub const Controller = struct {
    selected: u1 = 0,
    previous: u16 = 0,
    armed: bool = false,
    quiet_ticks: u8 = 0,
    auto_boot_cancelled: bool = false,
    pub fn activity(self: *const Controller, key: u16) bool {
        return key != 0 or self.previous != 0;
    }
    pub fn pollTimed(self: *Controller, key: u16, expired: bool) Action {
        if (self.activity(key)) self.auto_boot_cancelled = true;
        const action = self.poll(key);
        // An input at the deadline wins, including a power-key release.
        if (action != .none) return action;
        if (expired and !self.auto_boot_cancelled) return .boot_android;
        return .none;
    }
    // Qualcomm ButtonsDxe exposes held keys as repeated scan codes. Act on
    // release, not repeat. Ignore the power key used to turn the tablet on.
    pub fn poll(self: *Controller, key: u16) Action {
        if (!self.armed) {
            if (key == 0) {
                self.quiet_ticks +|= 1;
                if (self.quiet_ticks >= 6) self.armed = true;
            } else self.quiet_ticks = 0;
            self.previous = 0;
            return .none;
        }
        const released = self.previous != 0 and key == 0;
        const old = self.previous;
        self.previous = key;
        if (!released) return .none;
        switch (old) {
            1, 2, 0x80, 0x81 => {
                self.selected ^= 1;
                return .changed;
            },
            0x0102, 0xFFFF => return .confirm, // SCAN_SUSPEND / USB Enter
            else => return .none,
        }
    }
};
test "startup power and held volume never confirm or repeat" {
    var c = Controller{};
    for (0..30) |_| try std.testing.expectEqual(Action.none, c.poll(0x102));
    for (0..6) |_| _ = c.poll(0);
    for (0..30) |_| try std.testing.expectEqual(Action.none, c.poll(2));
    try std.testing.expectEqual(Action.changed, c.poll(0));
    try std.testing.expectEqual(@as(u1, 1), c.selected);
    _ = c.poll(0x102);
    try std.testing.expectEqual(Action.confirm, c.poll(0));
}
test "release to arm, unknown keys ignored, wrap and Enter" {
    var c = Controller{};
    for (0..6) |_| _ = c.poll(0);
    _ = c.poll(55);
    try std.testing.expectEqual(Action.none, c.poll(0));
    _ = c.poll(0x80);
    try std.testing.expectEqual(Action.changed, c.poll(0));
    _ = c.poll(1);
    try std.testing.expectEqual(Action.changed, c.poll(0));
    try std.testing.expectEqual(@as(u1, 0), c.selected);
    _ = c.poll(0xffff);
    try std.testing.expectEqual(Action.confirm, c.poll(0));
}
test "initial no-input timeout boots Android" {
    var c = Controller{};
    for (0..100) |_| try std.testing.expectEqual(Action.none, c.pollTimed(0, false));
    try std.testing.expectEqual(Action.boot_android, c.pollTimed(0, true));
}
test "a selection cancels automatic boot permanently until confirmation" {
    var c = Controller{};
    for (0..6) |_| _ = c.pollTimed(0, false);
    _ = c.pollTimed(2, false);
    try std.testing.expectEqual(Action.changed, c.pollTimed(0, false));
    try std.testing.expectEqual(@as(u1, 1), c.selected);
    try std.testing.expect(c.auto_boot_cancelled);
    for (0..1000) |_| try std.testing.expectEqual(Action.none, c.pollTimed(0, true));
    _ = c.pollTimed(0x102, true);
    try std.testing.expectEqual(Action.confirm, c.pollTimed(0, true));
    try std.testing.expectEqual(@as(u1, 1), c.selected);
}
test "input at initial deadline cancels timeout and power release confirms" {
    var c = Controller{};
    for (0..6) |_| _ = c.pollTimed(0, false);
    try std.testing.expectEqual(Action.none, c.pollTimed(2, true));
    try std.testing.expectEqual(Action.changed, c.pollTimed(0, true));
    try std.testing.expectEqual(Action.none, c.pollTimed(0x102, true));
    try std.testing.expectEqual(Action.confirm, c.pollTimed(0, true));
}
test "startup held power cancels auto boot without confirming" {
    var c = Controller{};
    for (0..120) |_| try std.testing.expectEqual(Action.none, c.pollTimed(0x102, true));
    for (0..6) |_| _ = c.pollTimed(0, false);
    try std.testing.expectEqual(Action.none, c.pollTimed(0, true));
}
test "even an unmapped input cancels automatic boot" {
    var c = Controller{};
    try std.testing.expectEqual(Action.none, c.pollTimed(0xfffe, true));
    for (0..1000) |_| try std.testing.expectEqual(Action.none, c.pollTimed(0, true));
}
