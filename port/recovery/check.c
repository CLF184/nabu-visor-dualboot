/* SPDX-License-Identifier: MIT
 * Host-only mock of UEFI storage. No physical disks or devices are opened.
 */
#define GNU_EFI_SETJMP_H
#include <efi.h>
#include <efilib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>

EFI_BOOT_SERVICES *BS;
EFI_RUNTIME_SERVICES *RT;
EFI_SYSTEM_TABLE *ST;
EFI_HANDLE LibImageHandle;
extern EFI_STATUS nabu_recovery_boot(void);
VOID ZeroMem(VOID *p,UINTN n){ memset(p,0,(size_t)n); }
VOID EFIAPI CopyMemC(VOID *p,const VOID *q,UINTN n){ memcpy(p,q,(size_t)n); }
VOID EFIAPI CopyMem_1(VOID *p,VOID *q,UINTN n){ memcpy(p,q,(size_t)n); }
INTN CompareMem(const VOID *p,const VOID *q,UINTN n){return memcmp(p,q,(size_t)n);}
static UINT8 disk[8192],original[8192],info[144];
static EFI_BLOCK_IO_MEDIA media;
static EFI_BLOCK_IO_PROTOCOL io;
static EFI_BOOT_SERVICES services;
static EFI_RUNTIME_SERVICES runtime;
static EFI_SYSTEM_TABLE table;
static jmp_buf reset_jump;
static int writes,flushes,resets,fail_write,return_reset,marker_bridge_calls;
static VOID *pools[8];
static UINTN pool_count;

static EFI_STATUS EFIAPI alloc(EFI_MEMORY_TYPE type,UINTN n,VOID **p){
    (void)type;*p=malloc((size_t)n);assert(*p);pools[pool_count++]=*p;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI release(VOID *p){
    for(UINTN i=0;i<pool_count;i++)if(pools[i]==p){pools[i]=NULL;break;}
    free(p);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI locate(EFI_LOCATE_SEARCH_TYPE type,EFI_GUID *g,VOID *key,UINTN *count,EFI_HANDLE **handles){
    (void)type;(void)g;(void)key;*count=1;
    alloc(EfiBootServicesData,sizeof(EFI_HANDLE),(VOID **)handles);
    (*handles)[0]=(EFI_HANDLE)1;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI protocol(EFI_HANDLE h,EFI_GUID *g,VOID **p){
    assert(h==(EFI_HANDLE)1);
    *p=g->Data1==0x8cf2f62c?(VOID *)info:(VOID *)&io;return EFI_SUCCESS;
}
static void validate(UINT32 id,EFI_LBA lba,UINTN n,VOID *buf){
    assert(id==77 && lba==0);
    assert(n==(media.BlockSize==512?2048:4096));
    assert((UINTN)buf%media.IoAlign==0);
}
static EFI_STATUS EFIAPI read_blocks(EFI_BLOCK_IO_PROTOCOL *self,UINT32 id,EFI_LBA lba,UINTN n,VOID *buf){
    assert(self==&io);validate(id,lba,n,buf);memcpy(buf,disk,(size_t)n);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI write_blocks(EFI_BLOCK_IO_PROTOCOL *self,UINT32 id,EFI_LBA lba,UINTN n,VOID *buf){
    assert(self==&io);validate(id,lba,n,buf);writes++;
    if(fail_write && writes==1){memcpy(disk,buf,100);return EFI_DEVICE_ERROR;}
    memcpy(disk,buf,(size_t)n);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI flush(EFI_BLOCK_IO_PROTOCOL *self){assert(self==&io);flushes++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI reboot(EFI_RESET_TYPE type,EFI_STATUS status,UINTN n,CHAR16 *data){
    assert(type==EfiResetWarm && status==EFI_SUCCESS && n==0 && data==NULL);resets++;
    if(!return_reset)longjmp(reset_jump,1);
    return EFI_DEVICE_ERROR;
}
/* The actual unchanged marker bridge is checked against the successful
 * Android candidate separately. Here check when Recovery invokes it. */
EFI_STATUS nabu_renegade_android(EFI_HANDLE handle,EFI_SYSTEM_TABLE *table){
    assert(handle==LibImageHandle && table==ST);
    assert(writes==1 && flushes==1 && memcmp(disk,"boot-recovery",13)==0);
    marker_bridge_calls++;
    return RT->ResetSystem(EfiResetWarm,EFI_SUCCESS,0,NULL);
}
static void init(UINTN block){
    for(UINTN i=0;i<pool_count;i++)if(pools[i])free(pools[i]);pool_count=0;
    memset(&media,0,sizeof(media));memset(info,0,sizeof(info));
    *(UINT32 *)(info+4)=2;const CHAR16 name[]=L"misc";memcpy(info+72,name,sizeof(name));
    for(size_t i=0;i<sizeof(disk);i++)disk[i]=(UINT8)((i*37+19)%251);
    memcpy(original,disk,sizeof(disk));
    media.MediaId=77;media.MediaPresent=1;media.LogicalPartition=1;media.BlockSize=(UINT32)block;media.IoAlign=4096;media.LastBlock=15;
    io.Media=&media;io.ReadBlocks=read_blocks;io.WriteBlocks=write_blocks;io.FlushBlocks=flush;
    services.AllocatePool=alloc;services.FreePool=release;services.LocateHandleBuffer=locate;services.HandleProtocol=protocol;
    runtime.ResetSystem=reboot;BS=&services;RT=&runtime;
    table.RuntimeServices=&runtime;ST=&table;
    writes=flushes=resets=fail_write=return_reset=marker_bridge_calls=0;
    LibImageHandle=(EFI_HANDLE)123;
}
static void check_success(UINTN block){
    init(block);
    if(setjmp(reset_jump)==0){nabu_recovery_boot();assert(0);}
    assert(writes==1 && flushes==1 && resets==1 && marker_bridge_calls==1);
    UINT8 expected[8192];memcpy(expected,original,sizeof(expected));
    memset(expected,0,32);memset(expected+64,0,768);
    memcpy(expected,"boot-recovery",13);memcpy(expected+64,"recovery\n",10);
    assert(memcmp(expected,disk,sizeof(disk))==0);
}
int main(void){
    check_success(512);check_success(4096);
    init(4096);media.ReadOnly=1;assert(nabu_recovery_boot()==EFI_WRITE_PROTECTED);assert(writes==0 && resets==0 && marker_bridge_calls==0);
    init(4096);fail_write=1;assert(nabu_recovery_boot()==EFI_DEVICE_ERROR);assert(writes==2 && resets==0 && marker_bridge_calls==0 && memcmp(disk,original,sizeof(disk))==0);
    init(4096);return_reset=1;assert(nabu_recovery_boot()==EFI_DEVICE_ERROR);assert(writes==2 && resets==1 && memcmp(disk,original,sizeof(disk))==0);
    for(UINTN i=0;i<pool_count;i++)if(pools[i])free(pools[i]);
    puts("PASS: Recovery delegates to the verified Android marker/reset bridge only after BCB write/flush/readback; 512/4096-byte preservation and failure restoration passed.");
    return 0;
}
