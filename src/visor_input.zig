// SPDX-License-Identifier: MIT
const Controller = @import("controller.zig").Controller;
pub const Key = extern struct { scan: u16 = 0, unicode: u16 = 0 };
pub const Translator = struct {
    controller: Controller = .{},
    pub fn translate(self: *Translator, scan: u16, key: *Key) bool {
        const old = self.controller.previous;
        const active = self.controller.activity(scan);
        key.* = .{};
        switch (self.controller.poll(scan)) {
            .changed => key.scan = if (old == 1 or old == 0x80) 1 else 2,
            .confirm => key.unicode = 13,
            else => {
                if (!active) return false;
                // SUCCESS cancels Visor's timeout, even for an unmapped key.
                key.scan = 0x7f;
            },
        }
        return true;
    }
};
