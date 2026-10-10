/* SPDX-License-Identifier: MIT
 * COFF startup only. Formatting, strings, memory and device paths use the
 * real GNU-EFI library sources rather than a reduced compatibility parser.
 */
#include <efi.h>
#include <efilib.h>
extern EFI_HANDLE LibImageHandle;
void InitializeLib(EFI_HANDLE handle,EFI_SYSTEM_TABLE *table) {
    ST=table;BS=table->BootServices;RT=table->RuntimeServices;
    LibImageHandle=handle;PoolAllocationType=EfiBootServicesData;
}
