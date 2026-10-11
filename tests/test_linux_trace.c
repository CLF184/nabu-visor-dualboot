#define GNU_EFI_SETJMP_H
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/visor_linux_trace.c"

int visor_boot_services_active = 1;
static EFI_FILE_PROTOCOL root, file;
static char log_data[4096];
static UINTN log_size, pos;
static int roots, files, old, fail, load_calls, start_calls, table_begins, table_ends;
static EFI_STATUS expected;
static char *strstr(const char *text,const char *needle) {
    for(;*text;text++){const char *a=text,*b=needle;while(*b && *a==*b){a++;b++;}if(!*b)return (char*)text;}return NULL;
}
static int eq(CHAR16 *a,CHAR16 *b) { while(*a && *a==*b){a++;b++;}return *a==*b; }
static EFI_STATUS EFIAPI close_file(EFI_FILE_PROTOCOL *f) { assert(f==&file);files--;return EFI_SUCCESS; }
static EFI_STATUS EFIAPI close_root(EFI_FILE_PROTOCOL *r) { assert(r==&root);roots--;return EFI_SUCCESS; }
static EFI_STATUS EFIAPI del(EFI_FILE_PROTOCOL *f) {close_file(f);if(fail==3)return EFI_WARN_DELETE_FAILURE;old=0;log_size=0;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI open_file(EFI_FILE_PROTOCOL *r,EFI_FILE_PROTOCOL **out,CHAR16 *path,UINT64 mode,UINT64 attr) {
    assert(r==&root && eq(path,TRACE_PATH) && !attr);
    if(fail==1)return EFI_WRITE_PROTECTED;
    if(!(mode&EFI_FILE_MODE_CREATE) && !old)return EFI_NOT_FOUND;
    old=1;files++;*out=&file;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI seek(EFI_FILE_PROTOCOL *f,UINT64 p) {assert(f==&file && p==~(UINT64)0);pos=log_size;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI write_file(EFI_FILE_PROTOCOL *f,UINTN *n,VOID *buf) {
    assert(f==&file && pos+*n<sizeof(log_data));
    if(fail==2)return EFI_DEVICE_ERROR;
    memcpy(log_data+pos,buf,*n);log_size=pos+*n;log_data[log_size]=0;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI flush(EFI_FILE_PROTOCOL *f) {assert(f==&file);return EFI_SUCCESS;}
EFI_FILE_PROTOCOL *efi_open_volume_root(EFI_HANDLE v) {assert(v==(EFI_HANDLE)1);assert(NABU_LINUX_TRACE);roots++;return &root;}
static EFI_STATUS EFIAPI load_image(BOOLEAN p,EFI_HANDLE parent,EFI_DEVICE_PATH *path,VOID *buffer,UINTN size,EFI_HANDLE *image) {
    assert(!roots && !files && !p && parent==(EFI_HANDLE)2 && path==(void*)3 && buffer==(void*)4 && size==5);
    load_calls++;*image=(EFI_HANDLE)6;return expected;
}
static EFI_STATUS EFIAPI start_image(EFI_HANDLE image,UINTN *size,CHAR16 **data) {
    assert(!roots && !files && image==(EFI_HANDLE)6 && !size && !data);
    start_calls++;return expected;
}
EFI_STATUS nabu_linux_table_begin(EFI_BOOT_SERVICES *bs,EFI_HANDLE image) { (void)bs;assert(image==(EFI_HANDLE)6);table_begins++;return EFI_SUCCESS; }
void nabu_linux_table_end(void) {table_ends++;}
int main(void) {
    #if !NABU_LINUX_TRACE
    EFI_BOOT_SERVICES bs={0};bs.LoadImage=load_image;bs.StartImage=start_image;
    EFI_HANDLE image=NULL;expected=EFI_ABORTED;
    assert(nabu_linux_load_image(&bs,0,(void*)2,(void*)3,(void*)4,5,&image)==expected);
    assert(nabu_linux_start_image(&bs,image,NULL,NULL)==expected && !table_begins);
    nabu_linux_trace_begin((void*)1,NULL);
    assert(nabu_linux_start_image(&bs,image,NULL,NULL)==expected);
    assert(table_begins==1 && table_ends==1 && !roots && !files && !log_size);
    nabu_linux_trace_end();
    assert(nabu_linux_start_image(&bs,image,NULL,NULL)==expected && table_begins==1);
    puts("Logging off: no ESP access; Linux metadata repair and original firmware status retained");
    return 0;
    #else
    root.Open=open_file;root.Close=close_root;file.Close=close_file;file.Delete=del;
    file.SetPosition=seek;file.Write=write_file;file.Flush=flush;
    EFI_BOOT_SERVICES bs={0};bs.LoadImage=load_image;bs.StartImage=start_image;
    EFI_HANDLE image=NULL;expected=EFI_SECURITY_VIOLATION;
    assert(nabu_linux_load_image(&bs,0,(void*)2,(void*)3,(void*)4,5,&image)==expected);
    assert(log_size==0 && load_calls==1);
    nabu_linux_trace_begin((void*)1,NULL);
    assert(nabu_linux_load_image(&bs,0,(void*)2,(void*)3,(void*)4,5,&image)==expected);
    assert(strstr(log_data,"LoadImage.return 0x800000000000001a"));
    expected=EFI_ABORTED;assert(nabu_linux_start_image(&bs,image,NULL,NULL)==expected);
    assert(strstr(log_data,"StartImage.before") && strstr(log_data,"StartImage.return 0x8000000000000015"));
    nabu_linux_trace_begin((void*)1,NULL);
    assert(!strstr(log_data,"StartImage.return"));
    for(fail=1;fail<=3;fail++) {
        nabu_linux_trace_begin((void*)1,NULL);
        assert(nabu_linux_start_image(&bs,image,NULL,NULL)==EFI_ABORTED);
        assert(!roots && !files);
    }
    fail=0;nabu_linux_trace_end();UINTN before=log_size;
    nabu_linux_trace("inactive",0);assert(before==log_size);
    assert(load_calls==2 && start_calls==4);
    puts("Linux trace: firmware arguments/status preserved, handles closed, failures non-fatal, old attempt reset");
    #endif
}
