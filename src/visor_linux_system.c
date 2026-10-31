/* SPDX-License-Identifier: MIT
 * Validate BDS-provided metadata before handing off to original rEFInd.
 * No system-table, runtime-variable or policy changes. */
#include "nabu_visor.h"
#include "efi_helpers.h"

EFI_STATUS nabu_linux_table_begin(EFI_BOOT_SERVICES *bs, EFI_HANDLE image) {
    EFI_GUID loaded_guid = LOADED_IMAGE_PROTOCOL;
    EFI_LOADED_IMAGE *loaded = NULL;
    EFI_STATUS status = bs->HandleProtocol(image, &loaded_guid, (void **)&loaded);
    if (EFI_ERROR(status)) return status;
    if (!loaded || !loaded->SystemTable) return EFI_INVALID_PARAMETER;
    EFI_SYSTEM_TABLE *table = loaded->SystemTable;
    nabu_linux_trace("SystemTable.vendor.missing", table->FirmwareVendor ? 0 : 1);
    if (table->FirmwareVendor) return EFI_SUCCESS;
    return EFI_UNSUPPORTED;
}
