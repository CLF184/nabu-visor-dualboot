# Third-party notices

The root MIT license applies to the project-owned nabu adaptation and build/update scripts.
Upstream code and derivative patch portions retain their upstream licenses:

- Visor-BootManager: BSD-2-Clause; see third_party/visor/LICENSE. The nabu patch modifies this source under its existing license.
- GNU-EFI: see third_party/gnu-efi/LICENSE and third_party/gnu-efi/licenses/. Compiled inc/lib files use the licenses identified there.
- Renegade edk2-msm: see third_party/renegade/LICENSE and the relevant source headers. Only Reboot2PayloadApp.c and the BootShim dualboot loops are used here.
- DualBootKernelPatcher: the provided nabu config/entry bytes originate from the previously used DuoWOA/WOA-msmnile toolchain. See https://github.com/Project-Aloha/DualBootKernelPatcher and its upstream licensing. Executables are not distributed by this repository.

Upstream assets are read from the pinned Visor submodule and generated into the local ESP package. Firmware input images, tool executables and compiled output images are not published in this source repository.
