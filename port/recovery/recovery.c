/* SPDX-License-Identifier: MIT
 * Nabu Recovery: AOSP BCB request, then the existing firmware warm reset.
 * Field layout and update semantics:
 * https://android.googlesource.com/platform/bootable/recovery/+/refs/heads/main/bootloader_message/bootloader_message.cpp
 * Only command[32] and recovery[768] change; status/stage/vendor data survive.
 */
#include <efi.h>
#include <efilib.h>
#include <efigpt.h>

/* UEFI 2.7 Partition Information protocol (EDK2 PartitionInfo.h). */
#pragma pack(push, 1)
typedef struct {
    UINT32 Revision, Type;
    UINT8 System, Reserved[7];
    EFI_PARTITION_ENTRY Gpt;
} NABU_PARTITION_INFO;
#pragma pack(pop)
_Static_assert(sizeof(NABU_PARTITION_INFO) == 144, "PartitionInfo ABI");
_Static_assert(__builtin_offsetof(NABU_PARTITION_INFO, Gpt.PartitionName) == 72, "GPT name ABI");

static EFI_GUID partition_guid = {0x8cf2f62c,0xbc9b,0x4821,{0x80,0x8d,0xec,0x9e,0xc4,0x21,0xa1,0xa0}};
static EFI_GUID block_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
static CHAR16 *last_error = L"Recovery request failed";
CHAR16 *nabu_recovery_error(void) { return last_error; }
/* Reuse the exact upstream Android marker writer and warm reset bridge. */
extern EFI_STATUS nabu_renegade_android(EFI_HANDLE, EFI_SYSTEM_TABLE *);
extern EFI_HANDLE LibImageHandle;

static int is_misc(const CHAR16 *name) {
    static const CHAR16 wanted[] = L"misc";
    for (UINTN i=0; i<5; i++) if(name[i]!=wanted[i]) return 0;
    return 1;
}

static EFI_STATUS find_misc(EFI_BLOCK_IO_PROTOCOL **result) {
    EFI_HANDLE *handles=NULL;
    UINTN count=0;
    EFI_STATUS status=BS->LocateHandleBuffer(ByProtocol,&partition_guid,NULL,&count,&handles);
    *result=NULL;
    if(EFI_ERROR(status)) { last_error=L"Recovery: partition information unavailable"; return status; }
    for(UINTN i=0; i<count; i++) {
        NABU_PARTITION_INFO *info=NULL;
        EFI_BLOCK_IO_PROTOCOL *io=NULL;
        if(EFI_ERROR(BS->HandleProtocol(handles[i],&partition_guid,(VOID **)&info)) || !info || info->Type!=2 || !is_misc(info->Gpt.PartitionName)) continue;
        if(EFI_ERROR(BS->HandleProtocol(handles[i],&block_guid,(VOID **)&io)) || !io || !io->Media || !io->Media->LogicalPartition) continue;
        if(*result) { BS->FreePool(handles); last_error=L"Recovery: multiple misc partitions"; return EFI_NO_MAPPING; }
        *result=io;
    }
    BS->FreePool(handles);
    if(!*result) { last_error=L"Recovery: misc partition not found"; return EFI_NOT_FOUND; }
    return EFI_SUCCESS;
}

static VOID *aligned_pool(UINTN size, UINTN alignment, VOID **allocation) {
    *allocation=NULL;
    if(EFI_ERROR(BS->AllocatePool(EfiBootServicesData,size+alignment-1,allocation))) return NULL;
    return (VOID *)(((UINTN)*allocation+alignment-1)&~(alignment-1));
}

static EFI_STATUS restore(EFI_BLOCK_IO_PROTOCOL *io,UINT32 media,UINTN size,VOID *old,VOID *verify) {
    EFI_STATUS s=io->WriteBlocks(io,media,0,size,old);
    if(EFI_ERROR(s)) return s;
    s=io->FlushBlocks(io);
    if(EFI_ERROR(s)) return s;
    s=io->ReadBlocks(io,media,0,size,verify);
    if(EFI_ERROR(s)) return s;
    return CompareMem(old,verify,size)==0?EFI_SUCCESS:EFI_DEVICE_ERROR;
}

EFI_STATUS nabu_recovery_boot(void) {
    EFI_BLOCK_IO_PROTOCOL *io=NULL;
    VOID *alloc[3]={NULL,NULL,NULL};
    UINT8 *old=NULL,*request=NULL,*verify=NULL;
    UINTN size=0,alignment=0;
    UINT32 media=0;
    int attempted_write=0;
    EFI_STATUS status;
    last_error=L"Recovery request failed";
    if(!BS || !RT || !RT->ResetSystem) { last_error=L"Recovery: reboot unavailable"; return EFI_UNSUPPORTED; }
    status=find_misc(&io);
    if(EFI_ERROR(status)) return status;
    if(!io->Media->MediaPresent) { last_error=L"Recovery: misc media unavailable"; return EFI_NO_MEDIA; }
    if(io->Media->ReadOnly) { last_error=L"Recovery: misc is read-only"; return EFI_WRITE_PROTECTED; }
    UINTN block=io->Media->BlockSize;
    alignment=io->Media->IoAlign;
    if(alignment<8) alignment=8;
    if(block<512 || block>65536 || (block&(block-1)) || alignment>1048576 || (alignment&(alignment-1)) || !io->ReadBlocks || !io->WriteBlocks || !io->FlushBlocks) {
        last_error=L"Recovery: unsupported misc block geometry"; return EFI_UNSUPPORTED;
    }
    size=(2048+block-1)&~(block-1);
    if(io->Media->LastBlock<size/block-1) { last_error=L"Recovery: misc partition too small"; return EFI_BAD_BUFFER_SIZE; }
    media=io->Media->MediaId;
    old=aligned_pool(size,alignment,&alloc[0]);
    request=aligned_pool(size,alignment,&alloc[1]);
    verify=aligned_pool(size,alignment,&alloc[2]);
    if(!old || !request || !verify) { last_error=L"Recovery: out of memory"; status=EFI_OUT_OF_RESOURCES; goto done; }
    status=io->ReadBlocks(io,media,0,size,old);
    if(EFI_ERROR(status)) { last_error=L"Recovery: misc read failed"; goto done; }
    CopyMem(request,old,size);
    /* Same update as AOSP update_bootloader_message_in_struct, no options. */
    ZeroMem(request,32);
    ZeroMem(request+64,768);
    CopyMem(request,"boot-recovery",13);
    CopyMem(request+64,"recovery\n",10);
    attempted_write=1;
    status=io->WriteBlocks(io,media,0,size,request);
    if(EFI_ERROR(status)) { last_error=L"Recovery: misc write failed"; goto done; }
    status=io->FlushBlocks(io);
    if(EFI_ERROR(status)) { last_error=L"Recovery: misc flush failed"; goto done; }
    status=io->ReadBlocks(io,media,0,size,verify);
    if(EFI_ERROR(status) || CompareMem(request,verify,size)!=0) { last_error=L"Recovery: misc verification failed"; status=EFI_DEVICE_ERROR; goto done; }
    /* Recovery also reaches the patched boot kernel on this tablet. The BCB
     * selects Recovery; the existing 128 markers bypass Visor once. Without
     * them, the user has to return to the menu and choose Android manually. */
    nabu_renegade_android(LibImageHandle,ST);
    last_error=L"Recovery: reboot returned; request restored";
    status=EFI_DEVICE_ERROR;
done:
    if(attempted_write && EFI_ERROR(restore(io,media,size,old,verify))) last_error=L"Recovery: failed to restore misc request";
    for(UINTN i=0;i<3;i++) if(alloc[i]) BS->FreePool(alloc[i]);
    return status;
}
