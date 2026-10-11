/* SPDX-License-Identifier: MIT
 * Host-only GOP/cache mocks: never maps a device framebuffer. */
#define GNU_EFI_SETJMP_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/visor_landscape.c"

EFI_BOOT_SERVICES *BS;
EFI_SYSTEM_TABLE *ST;
static EFI_BOOT_SERVICES services;
static nabu_cpu_cache_t cache;
static UINT32 framebuffer[80*80], expected[80*80], source[90*90];
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;
static EFI_GRAPHICS_OUTPUT_PROTOCOL gop;
static gui_state_t gui;
static int cache_available, fail_cache, cache_calls, blt_calls, pools, fail_pool;
static int raised, restored, session_recording;

void *efi_allocate_pool(UINTN n) { if (fail_pool) return NULL; pools++; return malloc(n); }
void efi_free_pool(void *p) { pools--; free(p); }
void efi_log(CHAR16 *text) { (void)text; }
VOID ZeroMem(VOID *p,UINTN n) { memset(p,0,n); }
VOID EFIAPI CopyMemC(VOID *p,const VOID *q,UINTN n) { memcpy(p,q,n); }
VOID EFIAPI CopyMem_1(VOID *p,VOID *q,UINTN n) { memcpy(p,q,n); }
int cap_session_active(void) { return session_recording; }

static EFI_STATUS EFIAPI locate(EFI_GUID *guid,VOID *registration,VOID **out) {
    (void)registration;
    assert(guid->Data1==0x26baccb1);
    *out=cache_available ? &cache : NULL;
    return cache_available ? EFI_SUCCESS : EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI flush(nabu_cpu_cache_t *self,
    EFI_PHYSICAL_ADDRESS start,UINT64 length,UINT32 type) {
    assert(self==&cache && type==0);
    assert(start>=(UINTN)framebuffer && length>0);
    assert(start+length<=(UINTN)framebuffer+mode.FrameBufferSize);
    cache_calls++;
    return fail_cache ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_TPL EFIAPI raise(EFI_TPL tpl) { assert(tpl==TPL_NOTIFY); raised++; return TPL_APPLICATION; }
static VOID EFIAPI restore(EFI_TPL tpl) { assert(tpl==TPL_APPLICATION); restored++; }
static UINT32 convert(UINT32 p) {
    if (info.PixelFormat!=PixelRedGreenBlueReserved8BitPerColor) return p;
    return (p&0xFF00FF00u)|((p>>16)&0xFF)|((p&0xFF)<<16);
}
static EFI_STATUS EFIAPI blt(EFI_GRAPHICS_OUTPUT_PROTOCOL *self,
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *buffer,EFI_GRAPHICS_OUTPUT_BLT_OPERATION op,
    UINTN sx,UINTN sy,UINTN dx,UINTN dy,UINTN w,UINTN h,UINTN delta) {
    assert(self==&gop && op==EfiBltBufferToVideo);
    blt_calls++;
    for (UINTN y=0;y<h;y++) for (UINTN x=0;x<w;x++)
        framebuffer[(dy+y)*info.PixelsPerScanLine+dx+x]=
            convert(((UINT32*)buffer)[(sy+y)*(delta/4)+sx+x]);
    return EFI_SUCCESS;
}
static void setup(UINTN width,UINTN height,UINTN pitch,int available,int rgb) {
    assert(!pools);
    memset(&services,0,sizeof(services));
    services.LocateProtocol=locate;services.RaiseTPL=raise;services.RestoreTPL=restore;
    BS=&services;cache.FlushDataCache=flush;
    memset(&info,0,sizeof(info));
    info.HorizontalResolution=width;info.VerticalResolution=height;
    info.PixelsPerScanLine=pitch;
    info.PixelFormat=rgb ? PixelRedGreenBlueReserved8BitPerColor : PixelBlueGreenRedReserved8BitPerColor;
    memset(&mode,0,sizeof(mode));mode.Info=&info;
    mode.FrameBufferBase=(UINTN)framebuffer;mode.FrameBufferSize=pitch*height*4;
    memset(&gop,0,sizeof(gop));gop.Mode=&mode;gop.Blt=blt;
    memset(&gui,0,sizeof(gui));gui.gop=&gop;gui.screen_width=width;gui.screen_height=height;
    for (UINTN i=0;i<80*80;i++) framebuffer[i]=expected[i]=0xDEADBEEF;
    for (UINTN i=0;i<90*90;i++) source[i]=0xA5000000u+(UINT32)(i*7919);
    cache_available=available;fail_cache=0;fail_pool=0;cache_calls=blt_calls=0;
    raised=restored=0;
}
static void draw(UINTN sx,UINTN sy,UINTN dx,UINTN dy,UINTN w,UINTN h,UINTN stride) {
    assert(gui.gop->Blt(gui.gop,(void*)source,EfiBltBufferToVideo,
                       sx,sy,dx,dy,w,h,stride*4)==EFI_SUCCESS);
    /* Independent row/column oracle also checks untouched pixels and padding. */
    for (UINTN row=0;row<h;row++) for (UINTN col=0;col<w;col++) {
        UINTN out_row=dx+col;
        UINTN out_col=info.HorizontalResolution-1-dy-row;
        expected[out_row*info.PixelsPerScanLine+out_col]=
            convert(source[(sy+row)*stride+sx+col]);
    }
    assert(memcmp(expected,framebuffer,sizeof(framebuffer))==0);
    assert(raised==restored);
}
int main(void) {
    /* Full screen and unaligned band updates across 32-pixel tile boundaries,
     * padded scanlines, both UEFI byte orders, and firmware fallback. */
    for (int available=0;available<2;available++) for (int rgb=0;rgb<2;rgb++) {
        setup(37,65,43,available,rgb);
        assert(nabu_landscape_begin(&gui)==EFI_SUCCESS);
        assert(gui.screen_width==65 && gui.screen_height==37);
        draw(0,0,0,0,65,37,65);
        draw(3,2,1,3,61,7,70);
        draw(0,0,64,36,1,1,1);
        assert(available ? (!blt_calls && !pools) : (blt_calls==3 && pools==1));
        nabu_landscape_end();assert(!pools);
    }
    /* Cache sync failure must immediately redraw via GOP and remain there. */
    setup(37,65,43,1,1);assert(nabu_landscape_begin(&gui)==EFI_SUCCESS);
    fail_cache=1;draw(0,0,0,0,65,37,65);
    assert(blt_calls==1 && !direct_buffer);
    draw(0,0,4,5,17,13,17);assert(blt_calls==2);
    nabu_landscape_end();assert(!pools);
    /* Unsupported/broken linear modes and cache preflight failure use GOP. */
    for (int reason=0;reason<5;reason++) {
        setup(37,65,43,1,0);
        if (reason==0) info.PixelFormat=PixelBltOnly;
        if (reason==1) mode.FrameBufferSize=4;
        if (reason==2) info.PixelsPerScanLine=36;
        if (reason==3) fail_cache=1;
        if (reason==4) mode.FrameBufferBase++;
        assert(nabu_landscape_begin(&gui)==EFI_SUCCESS && !direct_buffer);
        nabu_landscape_end();assert(!pools);
    }
    setup(37,65,43,1,0);assert(nabu_landscape_begin(&gui)==EFI_SUCCESS);
    int calls=cache_calls;
    assert(gui.gop->Blt(gui.gop,(void*)source,EfiBltBufferToVideo,0,0,64,0,2,1,0)==EFI_INVALID_PARAMETER);
    assert(gui.gop->Blt(gui.gop,(void*)source,EfiBltBufferToVideo,0,0,0,0,1,1,3)==EFI_INVALID_PARAMETER);
    assert(gui.gop->Blt(gui.gop,(void*)source,EfiBltBufferToVideo,2,0,0,0,2,1,12)==EFI_INVALID_PARAMETER);
    assert(gui.gop->Blt(gui.gop,(void*)source,EfiBltBufferToVideo,0,(UINTN)-1,0,0,1,1,0)==EFI_INVALID_PARAMETER);
    assert(gui.gop->Blt(gui.gop,(void*)source,EfiBltVideoToBltBuffer,0,0,0,0,1,1,0)==EFI_UNSUPPORTED);
    assert(cache_calls==calls && !blt_calls);
    fail_cache=1;fail_pool=1;
    assert(gui.gop->Blt(gui.gop,(void*)source,EfiBltBufferToVideo,0,0,0,0,1,1,0)==EFI_OUT_OF_RESOURCES);
    nabu_landscape_end();assert(!pools && raised==restored);
    /* Compare fused storage with the original scale/dim/color rules; source
     * transparency, repeated source pixels and tile edges are all exercised. */
    static UINT32 painted[80*80],scene[80*80];
    UINTN xm[65],ym[37];UINT8 dim[256];
    for (UINTN c=0;c<256;c++) dim[c]=(UINT8)(c*195/255);
    for (UINTN x=0;x<65;x++) xm[x]=x*32/65;
    for (UINTN y=0;y<37;y++) ym[y]=(y*19/37)*32;
    for (UINTN i=0;i<32*19;i++) {
        source[i]=0xFF000000u+(UINT32)(i*39119);
        if (i%21==0) source[i]&=0xFFFFFF;
    }
    for (int available=0;available<2;available++) for (int rgb=0;rgb<2;rgb++) {
        setup(37,65,43,available,rgb);
        /* setup rewrites source, so add transparent pixels again. */
        for (UINTN i=0;i<32*19;i++) if (i%21==0) source[i]&=0xFFFFFF;
        gui.backbuffer=painted;gui.scene_cache=scene;
        assert(nabu_landscape_begin(&gui)==EFI_SUCCESS && gui.nabu_portrait_buffer);
        assert(nabu_draw_anim_background(&gui,source,xm,ym,dim,0xFF123456));
        for (UINTN y=0;y<37;y++) for (UINTN x=0;x<65;x++) {
            UINT32 p=source[ym[y]+xm[x]],v=0xFF123456;
            if (p>>24) v=0xFF000000u | ((UINT32)((((p>>16)&255)*195)/255)<<16) |
                         ((UINT32)((((p>>8)&255)*195)/255)<<8) | ((p&255)*195)/255;
            assert(*get_pixel(&gui,x,y)==v);
            expected[x*43+36-y]=convert(v);
        }
        assert(gui.gop->Blt(gui.gop,(void*)painted,EfiBltBufferToVideo,
                           0,0,0,0,65,37,0)==EFI_SUCCESS);
        assert(memcmp(expected,framebuffer,sizeof(framebuffer))==0);
        assert(!portrait_buffer && !pools); /* No fullscreen transpose scratch. */
        for (UINTN x=0;x<65;x++) for (UINTN y=3;y<10;y++) {
            *get_pixel(&gui,x,y)=0xFF778899;
            expected[x*43+36-y]=convert(0xFF778899);
        }
        assert(gui.gop->Blt(gui.gop,(void*)painted,EfiBltBufferToVideo,
                           0,3,0,3,65,7,65*4)==EFI_SUCCESS);
        assert(memcmp(expected,framebuffer,sizeof(framebuffer))==0);
        if (available) {
            fail_cache=1;
            assert(gui.gop->Blt(gui.gop,(void*)painted,EfiBltBufferToVideo,
                               0,0,0,0,65,37,0)==EFI_SUCCESS);
            assert(!direct_buffer && !portrait_buffer);
        }
        memcpy(scene,painted,65*37*4);gui.scene_valid=1;
        fail_pool=1;
        assert(nabu_make_logical_buffer(&gui)==EFI_OUT_OF_RESOURCES && gui.nabu_portrait_buffer);
        fail_pool=0;
        assert(nabu_make_logical_buffer(&gui)==EFI_SUCCESS && !gui.nabu_portrait_buffer);
        for (UINTN y=0;y<37;y++) for (UINTN x=0;x<65;x++) {
            assert(convert(painted[y*65+x])==expected[x*43+36-y]);
            assert(scene[y*65+x]==painted[y*65+x]);
        }
        assert(gui.gop->Blt(gui.gop,(void*)painted,EfiBltBufferToVideo,
                           0,0,0,0,65,37,0)==EFI_SUCCESS);
        assert(memcmp(expected,framebuffer,sizeof(framebuffer))==0);
        nabu_landscape_end();assert(!pools && raised==restored);
    }
    setup(37,65,43,1,0);session_recording=1;
    assert(nabu_landscape_begin(&gui)==EFI_SUCCESS && !gui.nabu_portrait_buffer);
    assert(!nabu_draw_anim_background(&gui,source,xm,ym,dim,0));
    nabu_landscape_end();session_recording=0;
    puts("landscape: fused pixels, presentation, bands, capture layout, bounds and cache fallback passed");
    return 0;
}
