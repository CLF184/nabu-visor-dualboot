/* SPDX-License-Identifier: MIT: host-only ESP/file mocks; no device access. */
#define GNU_EFI_SETJMP_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/visor_linux.c"

static EFI_FILE_PROTOCOL root, file;
static EFI_HANDLE refind_vol = (EFI_HANDLE)1, linux_vol = (EFI_HANDLE)2;
static CHAR16 saved[CONFIG_CAP];
static UINTN saved_bytes, position;
static int loaded, pools, roots, files, same_volume, failure, target_missing, have_old;

static int eq(const CHAR16 *a, const CHAR16 *b) {
    while (*a && *a==*b) { a++; b++; } return *a==*b;
}
static int contains(const CHAR16 *text, const CHAR16 *needle) {
    for (; *text; text++) {
        const CHAR16 *a=text,*b=needle;
        while (*b && *a==*b) { a++; b++; }
        if (!*b) return 1;
    } return 0;
}
void *efi_allocate_pool(UINTN n) { pools++; return malloc(n); }
void efi_free_pool(void *p) { assert(p); pools--; free(p); }
UINTN efi_strlen16(CHAR16 *s) { UINTN n=0; while(s[n])n++; return n; }
int text_eq_ci(CHAR16 *a,const CHAR16 *b) { return eq(a,b); }
int mem_equal(const void *a,const void *b,UINTN n) { return !memcmp(a,b,n); }
void efi_print(CHAR16 *s,...) { (void)s; }
VOID ZeroMem(VOID *p,UINTN n) { memset(p,0,n); }
static EFI_STATUS EFIAPI close_file(EFI_FILE_PROTOCOL *p) { assert(p==&file); files--; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI close_root(EFI_FILE_PROTOCOL *p) { assert(p==&root); roots--; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI delete_file(EFI_FILE_PROTOCOL *p) { close_file(p); have_old=0; return failure==5?EFI_WARN_DELETE_FAILURE:EFI_SUCCESS; }
static EFI_STATUS EFIAPI open_file(EFI_FILE_PROTOCOL *p,EFI_FILE_PROTOCOL **out,CHAR16 *path,UINT64 mode,UINT64 attr) {
    assert(p==&root && eq(path,REFIND_CONFIG) && !attr);
    if (failure==1) return EFI_WRITE_PROTECTED;
    if (!(mode&EFI_FILE_MODE_CREATE) && !have_old) return EFI_NOT_FOUND;
    if (mode&EFI_FILE_MODE_CREATE) { have_old=1; saved_bytes=0; }
    *out=&file; files++; position=0; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI write_file(EFI_FILE_PROTOCOL *p,UINTN *n,VOID *buf) {
    assert(p==&file && *n<=sizeof(saved));
    if(failure==2) *n-=2;
    memcpy(saved,buf,*n);saved[*n/2]=0;saved_bytes=*n;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI flush_file(EFI_FILE_PROTOCOL *p) { assert(p==&file);return failure==3?EFI_DEVICE_ERROR:EFI_SUCCESS; }
static EFI_STATUS EFIAPI set_position(EFI_FILE_PROTOCOL *p,UINT64 pos) { assert(p==&file);position=pos;return EFI_SUCCESS; }
static EFI_STATUS EFIAPI read_file(EFI_FILE_PROTOCOL *p,UINTN *n,VOID *buf) {
    assert(p==&file && position==0 && *n==saved_bytes);
    memcpy(buf,saved,*n);if(failure==4)((CHAR16*)buf)[0]=0;return EFI_SUCCESS;
}
EFI_FILE_PROTOCOL *efi_open_volume_root(EFI_HANDLE h) { assert(h==linux_vol || h==refind_vol); roots++;return &root; }
efi_file_t *efi_fopen_uuid(CHAR16 *path,CHAR16 *uuid) {
    (void)uuid;if(target_missing)return NULL;
    efi_file_t *f=efi_allocate_pool(sizeof(*f));
    f->volume=eq(path,REFIND_IMAGE)||same_volume?refind_vol:linux_vol;
    f->root=efi_open_volume_root(f->volume);f->handle=NULL;return f;
}
efi_file_t *efi_fopen(CHAR16 *path) { return efi_fopen_uuid(path,NULL); }
void efi_fclose(efi_file_t *f) { f->root->Close(f->root);efi_free_pool(f); }
int efi_file_exists_root(EFI_FILE_PROTOCOL *p,CHAR16 *path) { assert(p==&root);return !target_missing && *path; }
CHAR16 *efi_handle_partition_uuid(EFI_HANDLE h) {
    assert(h==linux_vol);CHAR16 *s=efi_allocate_pool(74);
    memcpy(s,L"12345678-1234-1234-1234-123456789abc",74);return s;
}
efi_file_buffer_t *efi_load_file_on_handle(EFI_HANDLE h, CHAR16 *path) {
    assert(h==refind_vol && eq(path,BASE_CONFIG));
    const char text[]="timeout 5\ninclude themes/test/theme.conf\nlog_level 4\n";
    efi_file_buffer_t *buf=efi_allocate_pool(sizeof(*buf));
    buf->size=sizeof(text)-1;buf->data=efi_allocate_pool(buf->size);
    memcpy(buf->data,text,buf->size);return buf;
}
void nabu_linux_trace_begin(EFI_HANDLE volume,EFI_SYSTEM_TABLE *st) { assert(volume==refind_vol);(void)st; }
void nabu_linux_trace_end(void) {}
void nabu_linux_trace(const char *stage,EFI_STATUS status) { (void)stage;(void)status; }
EFI_STATUS visor_boot(boot_entry_t *e,EFI_SYSTEM_TABLE *st) {
    (void)st;loaded++;assert(!pools && !roots && !files);
    assert(eq(e->kernel_path,REFIND_IMAGE));assert(eq(e->cmdline,L"refind -c nabu-selected.conf"));
    assert(e->hp_volume==refind_vol && !e->initrd_path && !e->has_sha256);
    return EFI_ABORTED;
}
static int event_open, event_signals, event_failure;
static EFI_STATUS EFIAPI create_event(UINT32 type, EFI_TPL tpl,
    EFI_EVENT_NOTIFY notify, const VOID *context, const EFI_GUID *group, EFI_EVENT *out) {
    assert(type==EVT_NOTIFY_SIGNAL && tpl==TPL_CALLBACK && notify && !context);
    assert(group->Data1==0x02ce967a && group->Data2==0xdd7e && group->Data3==0x4ffc);
    if(event_failure==1)return EFI_OUT_OF_RESOURCES;
    assert(!event_open);event_open=1;*out=(EFI_EVENT)3;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI signal_event(EFI_EVENT event) {
    assert(event==(EFI_EVENT)3 && event_open);event_signals++;
    return event_failure==2?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI close_event(EFI_EVENT event) {
    assert(event==(EFI_EVENT)3 && event_open);event_open=0;return EFI_SUCCESS;
}
int main(void) {
    EFI_BOOT_SERVICES bs={0};bs.CreateEventEx=create_event;bs.SignalEvent=signal_event;bs.CloseEvent=close_event;
    EFI_SYSTEM_TABLE st={0};st.BootServices=&bs;
    root.Open=open_file;root.Close=close_root;
    file.Close=close_file;file.Delete=delete_file;file.Write=write_file;
    file.Flush=flush_file;file.Read=read_file;file.SetPosition=set_position;
    boot_entry_t e={0};e.name=L"Ubuntu";e.kernel_path=L"\\EFI\\ubuntu\\6.14.11.efi";
    e.cmdline=L"root=PARTLABEL=linux rw test=\"a b\" #, unicode=中文";
    e.initrd_path=L"\\EFI\\ubuntu\\initrd.img";
    assert(nabu_boot_linux(&e,&st)==EFI_ABORTED && loaded==1);
    assert(contains(saved,L"include themes/test/theme.conf"));
    assert(!contains(saved,L"include refind.conf"));
    assert(saved[0]==0xfeff && contains(saved,L"volume \"12345678-1234-1234-1234-123456789abc\""));
    assert(contains(saved,L"initrd \"\\EFI\\ubuntu\\initrd.img\""));
    assert(contains(saved,L"test=\"\"a b\"\" #, unicode=中文"));
    assert(event_signals==1 && !event_open);
    same_volume=1;e.hp_volume=refind_vol;e.initrd_path=NULL;
    e.kernel_path=L"\\EFI\\fedora\\shim.efi";e.cmdline=NULL;
    assert(nabu_boot_linux(&e,&st)==EFI_ABORTED && loaded==2);
    assert(!contains(saved,L"volume ") && !contains(saved,L"initrd ") && !contains(saved,L"options "));
    assert(event_signals==2 && !event_open);
    for(event_failure=1;event_failure<=2;event_failure++) {
        assert(EFI_ERROR(nabu_boot_linux(&e,&st)) && loaded==2 && !event_open);
    }
    event_failure=0;
    for(failure=1;failure<=5;failure++) {
        have_old=1;assert(EFI_ERROR(nabu_boot_linux(&e,&st)));assert(loaded==2);
        assert(!pools && !files && !roots);
    }
    failure=0;e.has_sha256=1;assert(nabu_boot_linux(&e,&st)==EFI_UNSUPPORTED && loaded==2);
    e.has_sha256=0;e.cmdline=L"a\nmenuentry bad";
    assert(nabu_boot_linux(&e,&st)==EFI_INVALID_PARAMETER && loaded==2);
    e.cmdline=NULL;target_missing=1;assert(nabu_boot_linux(&e,&st)==EFI_NOT_FOUND && loaded==2);
    assert(!pools && !files && !roots);
    CHAR16 text[128];handoff_config c={text,0,128};
    const UINT8 utf8[]={0xef,0xbb,0xbf,0xe4,0xb8,0xad};
    assert(append_base(&c,utf8,sizeof(utf8)) && text[0]==0x4e2d);
    c.used=0;const UINT8 utf16[]={0xff,0xfe,0x2d,0x4e};
    assert(append_base(&c,utf16,sizeof(utf16)) && text[0]==0x4e2d);
    c.used=0;const UINT8 invalid[]={0xef,0xbb,0xbf,0xc0,0x80};
    assert(!append_base(&c,invalid,sizeof(invalid)));
    puts("Linux handoff: target/volume/args, stale config removal, write/read failures and closed handles passed");
}
