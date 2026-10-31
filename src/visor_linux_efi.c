/* SPDX-License-Identifier: MIT
 * Check BDS metadata only for the selected rEFInd handoff. No file logging. */
#include "nabu_visor.h"

static int linux_handoff;
void nabu_linux_handoff_begin(void) { linux_handoff=1; }
void nabu_linux_handoff_end(void) { linux_handoff=0; }

EFI_STATUS nabu_linux_start_image(EFI_BOOT_SERVICES *bs,EFI_HANDLE image,
    UINTN *exit_size,CHAR16 **exit_data) {
    if(linux_handoff) {
        EFI_STATUS status=nabu_linux_table_begin(bs,image);
        if(EFI_ERROR(status))return status;
    }
    return bs->StartImage(image,exit_size,exit_data);
}
