/* SPDX-License-Identifier: MIT: host-only table/CRC mocks; no device access. */
#define GNU_EFI_SETJMP_H
#define VISOR_HOST_TEST 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/visor_platform.c"

EFI_BOOT_SERVICES *BS;
EFI_RUNTIME_SERVICES *RT;
EFI_SYSTEM_TABLE *ST;
static EFI_SYSTEM_TABLE firmware;
static int crc_calls, boot_calls, cleanup_calls, fail_crc;

static UINT32 crc32(const VOID *data, UINTN size) {
    const UINT8 *bytes=data;
    UINT32 crc=~(UINT32)0;
    for(UINTN i=0;i<size;i++) {
        crc^=bytes[i];
        for(int bit=0;bit<8;bit++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));
    }
    return ~crc;
}

static EFI_STATUS EFIAPI calculate_crc(VOID *data,UINTN size,UINT32 *out) {
    assert(data!=&firmware && size==sizeof(firmware));
    assert(((EFI_SYSTEM_TABLE*)data)->Hdr.CRC32==0);
    crc_calls++;
    if(fail_crc)return EFI_DEVICE_ERROR;
    *out=crc32(data,size);
    return EFI_SUCCESS;
}

EFI_STATUS efi_main(EFI_HANDLE handle,EFI_SYSTEM_TABLE *table) {
    assert(handle==(EFI_HANDLE)7 && table!=&firmware);
    assert(table->ConIn && table->ConIn!=firmware.ConIn);
    EFI_SYSTEM_TABLE expected=*table;
    UINT32 supplied=expected.Hdr.CRC32;
    expected.Hdr.CRC32=0;
    assert(supplied==crc32(&expected,expected.Hdr.HeaderSize));
    /* No other firmware fields may change in the menu's private copy. */
    expected=firmware;
    expected.ConIn=table->ConIn;
    expected.Hdr.CRC32=supplied;
    assert(!memcmp(&expected,table,sizeof(expected)));
    boot_calls++;
    return EFI_ABORTED;
}

VOID ZeroMem(VOID *p,UINTN n) { memset(p,0,n); }
UINT64 efi_get_tick(void) { return 0; }
EFI_STATUS NabuReadKey(EFI_INPUT_KEY *key) { (void)key;return EFI_NOT_READY; }
EFI_STATUS nabu_recovery_boot(void) { return EFI_UNSUPPORTED; }
EFI_STATUS nabu_landscape_begin(gui_state_t *gui) { (void)gui;return EFI_SUCCESS; }
void efi_log_close(void) { cleanup_calls++; }
void nabu_font_release(void) { cleanup_calls++; }
void nabu_landscape_end(void) { cleanup_calls++; }

int main(void) {
    EFI_BOOT_SERVICES services={0};
    SIMPLE_INPUT_INTERFACE input={0};
    services.CalculateCrc32=calculate_crc;
    firmware.Hdr.HeaderSize=sizeof(firmware);
    firmware.Hdr.CRC32=0x12345678;
    firmware.FirmwareVendor=L"Original firmware";
    firmware.FirmwareRevision=42;
    firmware.BootServices=&services;
    firmware.ConIn=&input;
    EFI_SYSTEM_TABLE original=firmware;
    assert(crc32("123456789",9)==0xcbf43926u);
    assert(nabu_visor_menu((EFI_HANDLE)7,&firmware)==EFI_ABORTED);
    assert(crc_calls==1 && boot_calls==1 && cleanup_calls==3);
    assert(!memcmp(&firmware,&original,sizeof(firmware)));

    fail_crc=1;
    assert(nabu_visor_menu((EFI_HANDLE)7,&firmware)==EFI_DEVICE_ERROR);
    assert(crc_calls==2 && boot_calls==1 && cleanup_calls==3);
    assert(!memcmp(&firmware,&original,sizeof(firmware)));
    services.CalculateCrc32=NULL;
    assert(nabu_visor_menu((EFI_HANDLE)7,&firmware)==EFI_UNSUPPORTED);
    services.CalculateCrc32=calculate_crc;
    firmware.Hdr.HeaderSize=sizeof(firmware)-1;
    assert(nabu_visor_menu((EFI_HANDLE)7,&firmware)==EFI_UNSUPPORTED);
    firmware.Hdr.HeaderSize=sizeof(firmware)+1;
    assert(nabu_visor_menu((EFI_HANDLE)7,&firmware)==EFI_UNSUPPORTED);
    firmware.Hdr.HeaderSize=original.Hdr.HeaderSize;
    firmware.BootServices=NULL;
    assert(nabu_visor_menu((EFI_HANDLE)7,&firmware)==EFI_INVALID_PARAMETER);
    assert(nabu_visor_menu((EFI_HANDLE)7,NULL)==EFI_INVALID_PARAMETER);
    assert(crc_calls==2 && boot_calls==1);
    puts("Platform table: valid CRC after input override, original untouched, service/header failures stop before menu");
}
