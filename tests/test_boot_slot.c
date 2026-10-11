/* SPDX-License-Identifier: MIT: host-only UFS/GPT mocks, no device access. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <Library/BootSlotLib.h>

extern EFI_STATUS NabuBootSlotInitialize(EFI_SYSTEM_TABLE *);
extern EFI_STATUS EFIAPI BootSlotMain(EFI_HANDLE, EFI_SYSTEM_TABLE *);
#define BLOCK 4096
#define SECTORS 128
#define HANDLES 6
static UINT8 disks[2][BLOCK*SECTORS], original[2][BLOCK*SECTORS];
static EFI_BOOT_SERVICES services;
static EFI_SYSTEM_TABLE table;
static EFI_BLOCK_IO_PROTOCOL io[HANDLES];
static EFI_BLOCK_IO_MEDIA media[HANDLES];
static EFI_PARTITION_INFO_PROTOCOL parts[HANDLES];
static EFI_MEM_CARDINFO_PROTOCOL card;
static int allocations, writes[2], single_slot, active_b;
#pragma pack(push,1)
typedef struct { VENDOR_DEVICE_PATH root; HARDDRIVE_DEVICE_PATH part; EFI_DEVICE_PATH end; } MOCK_PATH;
#pragma pack(pop)
static MOCK_PATH paths[HANDLES];
static int disk_index(int handle) { return handle >= 2 ? 1 : 0; }
static EFI_STATUS EFIAPI allocate(EFI_MEMORY_TYPE type, UINTN size, VOID **out) {
    assert(type==EfiBootServicesData); *out=malloc(size); assert(*out); allocations++; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI release(VOID *p) { assert(p && allocations); free(p); allocations--; return EFI_SUCCESS; }
static VOID EFIAPI copy(VOID *d, VOID *s, UINTN n) { memmove(d,s,n); }
static VOID EFIAPI fill(VOID *d, UINTN n, UINT8 value) { memset(d,value,n); }
static EFI_STATUS EFIAPI crc(VOID *data, UINTN size, UINT32 *out) {
    UINT32 value=0xffffffff; const UINT8 *p=data;
    for(UINTN i=0;i<size;i++) { value^=p[i]; for(int b=0;b<8;b++) value=(value>>1)^((value&1)?0xedb88320:0); }
    *out=~value; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI read_blocks(EFI_BLOCK_IO_PROTOCOL *self, UINT32 id, EFI_LBA lba, UINTN size, VOID *out) {
    int h=(int)(self-io); assert(h==0 || h==2); assert(id==media[h].MediaId);
    assert(size%BLOCK==0 && lba+size/BLOCK<=SECTORS);
    memcpy(out,disks[disk_index(h)]+lba*BLOCK,size); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI write_blocks(EFI_BLOCK_IO_PROTOCOL *self, UINT32 id, EFI_LBA lba, UINTN size, VOID *in) {
    int h=(int)(self-io), d=disk_index(h); assert(h==0 || h==2); assert(id==media[h].MediaId);
    /* Slot updates may touch only the existing primary/backup GPT blocks. */
    assert(size==5*BLOCK && (lba==1 || lba==123));
    memcpy(disks[d]+lba*BLOCK,in,size); writes[d]++; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI get_card(EFI_MEM_CARDINFO_PROTOCOL *self, MEM_CARD_INFO *info) {
    assert(self==&card); memset(info,0,sizeof(*info)); memcpy(info->card_type,"UFS",3); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI boot_lun(EFI_MEM_CARDINFO_PROTOCOL *self, UINT32 *lun) { assert(self==&card); *lun=active_b?2:1; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI set_lun(EFI_MEM_CARDINFO_PROTOCOL *self, UINT32 lun) {
    (void)self;(void)lun; assert(!"unexpected boot LUN switch"); return EFI_DEVICE_ERROR;
}
static EFI_STATUS EFIAPI locate(EFI_GUID *guid, VOID *registration, VOID **out) {
    assert(!registration);
    if(CompareGuid(guid,&gEfiMemCardInfoProtocolGuid)) { *out=&card; return EFI_SUCCESS; }
    return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI locate_handles(EFI_LOCATE_SEARCH_TYPE type, EFI_GUID *guid, VOID *registration, UINTN *count, EFI_HANDLE **out) {
    assert(type==ByProtocol && !registration && CompareGuid(guid,&gEfiBlockIoProtocolGuid));
    UINTN n=single_slot?HANDLES-1:HANDLES;
    allocate(EfiBootServicesData,n*sizeof(**out),(VOID**)out); *count=n;
    UINTN i=0;for(int h=0;h<HANDLES;h++) if(!(single_slot && h==4)) (*out)[i++]=(EFI_HANDLE)(UINTN)(h+1);
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI handle_protocol(EFI_HANDLE handle, EFI_GUID *guid, VOID **out) {
    int h=(int)(UINTN)handle-1; assert(h>=0 && h<HANDLES);
    if(CompareGuid(guid,&gEfiBlockIoProtocolGuid)) { *out=&io[h]; return EFI_SUCCESS; }
    if(CompareGuid(guid,&gEfiDevicePathProtocolGuid)) { *out=&paths[h]; return EFI_SUCCESS; }
    if(CompareGuid(guid,&gEfiMemCardInfoProtocolGuid)) { *out=&card; return EFI_SUCCESS; }
    if(CompareGuid(guid,&gEfiPartitionInfoProtocolGuid) && h!=0 && h!=2) { *out=&parts[h]; return EFI_SUCCESS; }
    return EFI_UNSUPPORTED;
}
static EFI_PARTITION_ENTRY *entries(int disk,int backup) { return (VOID*)(disks[disk]+(backup?123:2)*BLOCK); }
static EFI_PARTITION_TABLE_HEADER *header(int disk,int backup) { return (VOID*)(disks[disk]+(backup?127:1)*BLOCK); }
static void make_entry(EFI_PARTITION_ENTRY *entry, const CHAR16 *name, UINT64 attr, int number) {
    memset(entry,0,sizeof(*entry)); entry->PartitionTypeGUID.Data1=0x1234+number;
    entry->UniquePartitionGUID.Data1=0x5678+number; entry->StartingLBA=8+number*4; entry->EndingLBA=entry->StartingLBA+3;
    memcpy(entry->PartitionName,name,(StrLen(name)+1)*sizeof(*name)); entry->Attributes=attr;
}
static void make_gpt(int disk) {
    for(int backup=0;backup<2;backup++) {
        EFI_PARTITION_TABLE_HEADER *h=header(disk,backup); memset(h,0,sizeof(*h));
        memcpy(&h->Header.Signature,"EFI PART",8);h->Header.Revision=0x10000;h->Header.HeaderSize=92;
        h->MyLBA=backup?127:1;h->AlternateLBA=backup?1:127;h->FirstUsableLBA=6;h->LastUsableLBA=122;
        h->DiskGUID.Data1=0x9876+disk;h->PartitionEntryLBA=backup?123:2;
        h->NumberOfPartitionEntries=128;h->SizeOfPartitionEntry=128;
        crc(entries(disk,backup),16384,&h->PartitionEntryArrayCRC32);crc(h,92,&h->Header.CRC32);
    }
}
static void refresh_partition_info(void) {
    parts[1].Info.Gpt=entries(0,0)[0];
    for(int h=3;h<HANDLES;h++) parts[h].Info.Gpt=entries(1,0)[h-3];
}
static void validate_gpt(void) {
    for(int d=0;d<2;d++)for(int b=0;b<2;b++) {
        EFI_PARTITION_TABLE_HEADER *h=header(d,b);UINT32 value, old=h->Header.CRC32;
        crc(entries(d,b),16384,&value); assert(value==h->PartitionEntryArrayCRC32);
        h->Header.CRC32=0;crc(h,92,&value);h->Header.CRC32=old;assert(value==old);
    }
}
static void init(void) {
    services.AllocatePool=allocate;services.FreePool=release;services.CopyMem=copy;services.SetMem=fill;
    services.CalculateCrc32=crc;services.LocateProtocol=locate;services.LocateHandleBuffer=locate_handles;services.HandleProtocol=handle_protocol;
    table.BootServices=&services;
    card.Revision=EFI_MEM_CARD_INFO_PROTOCOL_REVISION;card.GetCardInfo=get_card;card.GetBootLU=boot_lun;card.SetBootLU=set_lun;
    make_entry(&entries(0,0)[0],L"esp",0x2000000000000001ULL,1);
    make_entry(&entries(1,0)[0],L"boot_a",PART_ATT_PRIORITY_VAL|PART_ATT_ACTIVE_VAL|(1ULL<<PART_ATT_MAX_RETRY_CNT_BIT)|0x1000000000000001ULL,2);
    make_entry(&entries(1,0)[1],single_slot?L"other":L"boot_b",(2ULL<<PART_ATT_PRIORITY_BIT)|PART_ATT_UNBOOTABLE_VAL|0x2000000000000004ULL,3);
    make_entry(&entries(1,0)[2],L"userdata",0x8000000000000004ULL,4);
    if(active_b) {
        /* Real pre-flash snapshot: A unsuccessful/retry 1, B active/unsuccessful/retry 5. */
        entries(1,0)[0].Attributes=(2ULL<<PART_ATT_PRIORITY_BIT)|(1ULL<<PART_ATT_MAX_RETRY_CNT_BIT)|0x1000000000000001ULL;
        entries(1,0)[1].Attributes=PART_ATT_PRIORITY_VAL|PART_ATT_ACTIVE_VAL|(5ULL<<PART_ATT_MAX_RETRY_CNT_BIT)|0x2000000000000004ULL;
    }
    for(int d=0;d<2;d++) { memcpy(entries(d,1),entries(d,0),16384);make_gpt(d); }
    refresh_partition_info();
    for(int h=0;h<HANDLES;h++) {
        int root=h==0 || h==2;
        io[h].Media=&media[h];io[h].ReadBlocks=read_blocks;io[h].WriteBlocks=write_blocks;
        media[h].MediaPresent=1;media[h].LogicalPartition=!root;media[h].MediaId=h+10;media[h].BlockSize=BLOCK;media[h].LastBlock=SECTORS-1;
        paths[h].root.Header.Type=HARDWARE_DEVICE_PATH;paths[h].root.Header.SubType=HW_VENDOR_DP;paths[h].root.Header.Length[0]=sizeof(VENDOR_DEVICE_PATH);
        paths[h].root.Guid=h<2?gEfiUfsLU0Guid:gEfiUfsLU4Guid;
        EFI_DEVICE_PATH *end=root?(VOID*)&paths[h].part:&paths[h].end;
        end->Type=END_DEVICE_PATH_TYPE;end->SubType=END_ENTIRE_DEVICE_PATH_SUBTYPE;end->Length[0]=sizeof(*end);
        if(!root) { paths[h].part.Header.Type=MEDIA_DEVICE_PATH;paths[h].part.Header.SubType=MEDIA_HARDDRIVE_DP;paths[h].part.Header.Length[0]=sizeof(HARDDRIVE_DEVICE_PATH);paths[h].part.MBRType=2;parts[h].Type=2; }
    }
    memcpy(original,disks,sizeof(disks));
}
int main(int argc,char **argv) {
    single_slot=argc>1 && !strcmp(argv[1],"single");active_b=argc>1 && !strcmp(argv[1],"active-b");init();assert(NabuBootSlotInitialize(&table)==EFI_SUCCESS);
    assert(BootSlotMain((void*)1,&table)==EFI_SUCCESS && allocations==0);
    if(single_slot) { assert(!memcmp(original,disks,sizeof(disks)) && writes[0]==0 && writes[1]==0);puts("PASS: non-A/B media unchanged");return 0; }
    assert(writes[0]==0 && writes[1]>0 && writes[1]%2==0);
    validate_gpt();
    assert(!memcmp(original[0],disks[0],sizeof(disks[0])));
    for(int backup=0;backup<2;backup++) {
        EFI_PARTITION_ENTRY *e=entries(1,backup);
        int current=active_b?1:0,inactive=1-current;
        assert((e[current].Attributes & (PART_ATT_SUCCESSFUL_VAL|PART_ATT_ACTIVE_VAL|PART_ATT_MAX_RETRY_COUNT_VAL))==(PART_ATT_SUCCESSFUL_VAL|PART_ATT_ACTIVE_VAL|PART_ATT_MAX_RETRY_COUNT_VAL));
        assert(!(e[current].Attributes & PART_ATT_UNBOOTABLE_VAL));
        assert(!(e[inactive].Attributes & (PART_ATT_UNBOOTABLE_VAL|PART_ATT_SUCCESSFUL_VAL|PART_ATT_ACTIVE_VAL)));
        EFI_PARTITION_ENTRY *old=(VOID*)(original[1]+(backup?123:2)*BLOCK);
        assert((e[inactive].Attributes & PART_ATT_MAX_RETRY_COUNT_VAL)==PART_ATT_MAX_RETRY_COUNT_VAL);
        assert(!memcmp(&e[2],&old[2],126*sizeof(*e)));
        for(int i=0;i<2;i++) { UINT64 attr=e[i].Attributes;e[i].Attributes=old[i].Attributes;assert(!memcmp(&e[i],&old[i],sizeof(*e)));e[i].Attributes=attr; }
    }
    /* Prove all bytes outside the slot attributes and GPT CRCs survive. */
    UINT8 expected[BLOCK*SECTORS];memcpy(expected,original[1],sizeof(expected));
    for(int b=0;b<2;b++) {
        size_t offset=(b?123:2)*BLOCK;for(int i=0;i<2;i++)memcpy(expected+offset+i*128+48,(UINT8*)&entries(1,b)[i]+48,8);
        offset=(b?127:1)*BLOCK;memcpy(expected+offset+16,disks[1]+offset+16,4);memcpy(expected+offset+88,disks[1]+offset+88,4);
    }
    assert(!memcmp(expected,disks[1],sizeof(expected)));
    int count=writes[1];refresh_partition_info();assert(BootSlotMain((void*)1,&table)==EFI_SUCCESS && allocations==0);
    assert(writes[1]==count);validate_gpt();
    if(active_b) puts("PASS: pre-flash B-slot snapshot; B successful/retry 7; A unsuccessful/retry 7");
    puts("PASS: upstream BootSlotDxe on UFS LUN 4; both GPT CRCs valid; payload/other LUN preserved; successful slot is idempotent");
}
