/* SPDX-License-Identifier: MIT
 * Fill metadata normally installed by the supplied Project Silicium BDS.
 * Scope: Linux rEFInd handoff; no runtime variables or policy changes. */
#include "nabu_visor.h"
#include "efi_helpers.h"

static EFI_SYSTEM_TABLE *changed_table;
static CHAR16 *saved_vendor, *new_vendor;
static UINT32 saved_revision;
static EFI_BOOT_SERVICES *table_bs;

EFI_STATUS nabu_linux_table_begin(EFI_BOOT_SERVICES *bs, EFI_HANDLE image) {
    EFI_GUID loaded_guid = LOADED_IMAGE_PROTOCOL;
    EFI_LOADED_IMAGE *loaded = NULL;
    EFI_STATUS status = bs->HandleProtocol(image, &loaded_guid, (void **)&loaded);
    if (EFI_ERROR(status)) return status;
    if (!loaded || !loaded->SystemTable) return EFI_INVALID_PARAMETER;
    EFI_SYSTEM_TABLE *table = loaded->SystemTable;
    nabu_linux_trace("SystemTable.vendor.missing", table->FirmwareVendor ? 0 : 1);
    if (table->FirmwareVendor) return EFI_SUCCESS;
    if (changed_table || table->Hdr.HeaderSize != sizeof(*table))
        return EFI_UNSUPPORTED;
    const CHAR16 vendor[] = L"Project Silicium";
    CHAR16 *copy = NULL;
    status = bs->AllocatePool(EfiRuntimeServicesData, sizeof(vendor), (void **)&copy);
    if (EFI_ERROR(status)) return status;
    for (UINTN i = 0; i < sizeof(vendor)/sizeof(*vendor); i++) copy[i] = vendor[i];
    UINT32 old_crc = table->Hdr.CRC32, revision = table->FirmwareRevision;
    CHAR16 *previous = table->FirmwareVendor;
    table->FirmwareVendor = copy;
    if (!revision) table->FirmwareRevision = 0x10000;
    table->Hdr.CRC32 = 0;
    status = bs->CalculateCrc32(table, sizeof(*table), &table->Hdr.CRC32);
    if (EFI_ERROR(status)) {
        table->FirmwareVendor = previous; table->FirmwareRevision = revision;
        table->Hdr.CRC32 = old_crc; bs->FreePool(copy);
        return status;
    }
    changed_table = table; saved_vendor = previous; saved_revision = revision;
    new_vendor = copy; table_bs = bs;
    return EFI_SUCCESS;
}

void nabu_linux_table_end(void) {
    if (!changed_table || !visor_boot_services_active) return;
    changed_table->FirmwareVendor = saved_vendor;
    changed_table->FirmwareRevision = saved_revision;
    /* Child EFI programs may have installed configuration tables. Recompute
     * the current table's CRC instead of restoring a now-stale old checksum. */
    changed_table->Hdr.CRC32 = 0;
    EFI_STATUS status = table_bs->CalculateCrc32(changed_table,
        sizeof(*changed_table), &changed_table->Hdr.CRC32);
    nabu_linux_trace("SystemTable.restore", status);
    table_bs->FreePool(new_vendor);
    changed_table = NULL; new_vendor = NULL; table_bs = NULL;
}
