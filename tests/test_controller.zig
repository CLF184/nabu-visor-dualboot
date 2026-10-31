// SPDX-License-Identifier: MIT
const std = @import("std");
const controller = @import("controller");
const Controller = controller.Controller;
const Action = controller.Action;

test "startup power and held volume never confirm or repeat" {
    var c = Controller{};
    for (0..30) |_| try std.testing.expectEqual(Action.none, c.poll(0x102));
    for (0..6) |_| _ = c.poll(0);
    for (0..30) |_| try std.testing.expectEqual(Action.none, c.poll(2));
    try std.testing.expectEqual(Action.changed, c.poll(0));
    _ = c.poll(0x102);
    try std.testing.expectEqual(Action.confirm, c.poll(0));
}

test "release to arm, unknown keys ignored and Enter" {
    var c = Controller{};
    for (0..6) |_| _ = c.poll(0);
    _ = c.poll(55);
    try std.testing.expectEqual(Action.none, c.poll(0));
    _ = c.poll(0x80);
    try std.testing.expectEqual(Action.changed, c.poll(0));
    _ = c.poll(1);
    try std.testing.expectEqual(Action.changed, c.poll(0));
    _ = c.poll(0xffff);
    try std.testing.expectEqual(Action.confirm, c.poll(0));
}
