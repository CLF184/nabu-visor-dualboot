#define GNU_EFI_SETJMP_H
#define NABU_ESP_APPLICATION 1
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../src/visor_linux_system.c"
int visor_boot_services_active=1;
static EFI_SYSTEM_TABLE table;
static EFI_LOADED_IMAGE loaded;
static int pools, fail, crc_calls;
void nabu_linux_trace(const char *stage,EFI_STATUS status) {(void)stage;(void)status;}
static EFI_STATUS EFIAPI protocol(EFI_HANDLE image,EFI_GUID *guid,VOID **out) {
    assert(image==(EFI_HANDLE)1 && guid->Data1==0x5b1b31a1);
    *out=&loaded;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI alloc(EFI_MEMORY_TYPE type,UINTN size,VOID **out) {
    assert(type==EfiRuntimeServicesData);
    if(fail==1)return EFI_OUT_OF_RESOURCES;
    pools++;*out=malloc(size);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI release(VOID *p) {assert(p && pools);pools--;free(p);return EFI_SUCCESS;}
static EFI_STATUS EFIAPI crc(VOID *p,UINTN size,UINT32 *out) {
    assert(p==&table && size==sizeof(table) && !table.Hdr.CRC32);crc_calls++;
    if(fail==2)return EFI_DEVICE_ERROR;
    /* Mock CRC tracks fields changed by EFI child programs too. */
    *out=100+table.NumberOfTableEntries+(table.FirmwareVendor?10:0)+table.FirmwareRevision;
    return EFI_SUCCESS;
}
int main(void) {
    EFI_BOOT_SERVICES bs={0};bs.HandleProtocol=protocol;bs.AllocatePool=alloc;bs.FreePool=release;bs.CalculateCrc32=crc;
    loaded.SystemTable=&table;table.Hdr.HeaderSize=sizeof(table);table.Hdr.CRC32=123;
    assert(nabu_linux_table_begin(&bs,(void*)1)==EFI_UNSUPPORTED);
    assert(!pools && !crc_calls && !table.FirmwareVendor);
    assert(!table.FirmwareRevision && table.Hdr.CRC32==123);
    table.FirmwareVendor=L"Existing firmware";table.FirmwareRevision=42;
    int before=crc_calls;assert(nabu_linux_table_begin(&bs,(void*)1)==EFI_SUCCESS);
    nabu_linux_table_end();assert(!pools && table.FirmwareRevision==42 && crc_calls==before);
    table.FirmwareVendor=NULL;table.Hdr.HeaderSize=0;
    assert(nabu_linux_table_begin(&bs,(void*)1)==EFI_UNSUPPORTED && !pools);
    puts("ESP Linux system table: original BDS metadata preserved, missing vendor rejected without writes");
}
