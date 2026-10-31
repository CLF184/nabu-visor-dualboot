#define GNU_EFI_SETJMP_H
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/visor_linux_system.c"
static EFI_SYSTEM_TABLE table;
static EFI_LOADED_IMAGE loaded;
static EFI_STATUS protocol_status;
static EFI_STATUS EFIAPI protocol(EFI_HANDLE image,EFI_GUID *guid,VOID **out) {
    assert(image==(EFI_HANDLE)1 && guid->Data1==0x5b1b31a1);
    *out=&loaded;return protocol_status;
}
int main(void) {
    EFI_BOOT_SERVICES bs={0};bs.HandleProtocol=protocol;
    loaded.SystemTable=&table;table.Hdr.HeaderSize=sizeof(table);table.Hdr.CRC32=123;
    EFI_SYSTEM_TABLE original=table;
    assert(nabu_linux_table_begin(&bs,(void*)1)==EFI_UNSUPPORTED);
    assert(!memcmp(&table,&original,sizeof(table)));
    table.FirmwareVendor=L"Existing firmware";table.FirmwareRevision=42;
    original=table;
    assert(nabu_linux_table_begin(&bs,(void*)1)==EFI_SUCCESS);
    assert(!memcmp(&table,&original,sizeof(table)));
    loaded.SystemTable=NULL;
    assert(nabu_linux_table_begin(&bs,(void*)1)==EFI_INVALID_PARAMETER);
    protocol_status=EFI_ACCESS_DENIED;
    assert(nabu_linux_table_begin(&bs,(void*)1)==EFI_ACCESS_DENIED);
    assert(!memcmp(&table,&original,sizeof(table)));
    puts("ESP Linux system table: original BDS metadata preserved, missing vendor rejected without writes");
}
