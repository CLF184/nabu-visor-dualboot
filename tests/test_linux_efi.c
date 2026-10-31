/* SPDX-License-Identifier: MIT: host-only EFI handoff checks. */
#define GNU_EFI_SETJMP_H
#include <assert.h>
#include <stdio.h>
#include "../src/visor_linux_efi.c"

static int starts,checks;
static EFI_STATUS check_status;
static UINTN exit_size;
static CHAR16 *exit_data;

EFI_STATUS nabu_linux_table_begin(EFI_BOOT_SERVICES *bs,EFI_HANDLE image) {
    assert(bs && image==(EFI_HANDLE)6);
    checks++;return check_status;
}
static EFI_STATUS EFIAPI start_image(EFI_HANDLE image,UINTN *size,CHAR16 **data) {
    assert(image==(EFI_HANDLE)6 && size==&exit_size && data==&exit_data);
    starts++;return EFI_ABORTED;
}
int main(void) {
    EFI_BOOT_SERVICES bs={0};bs.StartImage=start_image;
    assert(nabu_linux_start_image(&bs,(EFI_HANDLE)6,&exit_size,&exit_data)==EFI_ABORTED);
    assert(starts==1 && checks==0);
    nabu_linux_handoff_begin();
    assert(nabu_linux_start_image(&bs,(EFI_HANDLE)6,&exit_size,&exit_data)==EFI_ABORTED);
    assert(starts==2 && checks==1);
    check_status=EFI_UNSUPPORTED;
    assert(nabu_linux_start_image(&bs,(EFI_HANDLE)6,&exit_size,&exit_data)==EFI_UNSUPPORTED);
    assert(starts==2 && checks==2);
    nabu_linux_handoff_end();
    assert(nabu_linux_start_image(&bs,(EFI_HANDLE)6,&exit_size,&exit_data)==EFI_ABORTED);
    assert(starts==3 && checks==2);
    puts("EFI handoff: original arguments/status retained, Linux-only check, failed metadata blocks StartImage");
}
