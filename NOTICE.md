# Third-party notices

The root MIT license applies to the project-owned nabu adaptation and build/update scripts.
Upstream code and derivative patch portions retain their upstream licenses:

- Visor-BootManager: BSD-2-Clause; see third_party/visor/LICENSE. The nabu patch modifies this source under its existing license.
- GNU-EFI: see third_party/gnu-efi/LICENSE and third_party/gnu-efi/licenses/. Compiled inc/lib files use the licenses identified there.
- Renegade edk2-msm: see third_party/renegade/LICENSE and the relevant source headers. Reboot2PayloadApp.c and the BootShim dualboot loops are used for Android handoff. BootSlotDxe is compiled as an independent DXE module from the pinned, unchanged GPLDrivers/Drivers/BootSlotDxe/BootSlotDxe.c (GPL-3.0-or-later, Copyright (C) 2021 Renard Gold) and GPLDrivers/Library/BootSlotLib sources. Library sources retain their Linux Foundation BSD notices. See port/boot-slot/LICENSE.txt, NOTICE.txt and upstream.json. Corresponding source is supplied by the pinned third_party/renegade submodule together with boot_slot_build.py and the bridge sources. Generated binary releases include BootSlotDxe-LICENSE.txt and BootSlotDxe-NOTICE.txt; the independent module's GPL license is not replaced by the root MIT license.
- DualBootKernelPatcher: the provided nabu config/entry bytes originate from the previously used DuoWOA/WOA-msmnile toolchain. See https://github.com/Project-Aloha/DualBootKernelPatcher and its upstream licensing. Executables are not distributed by this repository.

Upstream assets are read from the pinned Visor submodule and generated into the local ESP package. Firmware input images, tool executables and compiled output images are not published in this source repository.

Optional Linux ESP packages reuse rEFInd-nabu binaries/resources from the user-supplied TheMojoMan/xiaomi-nabu efi.tar. rEFInd/rEFIt notices are in port/refind/LICENSE.txt; bundled filesystem drivers and themes retain their own licenses. Source: https://github.com/rodriguezst/rEFInd-nabu. Binary assets are not committed to this source repository.
