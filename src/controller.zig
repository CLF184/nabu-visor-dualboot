// SPDX-License-Identifier: MIT
pub const Action = enum { none, changed, confirm };
pub const Controller = struct {
    previous: u16 = 0,
    armed: bool = false,
    quiet_ticks: u8 = 0,
    pub fn activity(self: *const Controller, key: u16) bool {
        return key != 0 or self.previous != 0;
    }
    // Qualcomm ButtonsDxe repeats held keys. Act on release and ignore
    // the startup power key until six quiet polls have armed the input.
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
        return switch (old) {
            1, 2, 0x80, 0x81 => .changed,
            0x0102, 0xFFFF => .confirm, // SCAN_SUSPEND / USB Enter
            else => .none,
        };
    }
};
