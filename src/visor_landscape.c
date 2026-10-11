/* SPDX-License-Identifier: MIT
 * Present a landscape GOP to Visor over Nabu's portrait GOP. Firmware modes
 * and the upstream menu/event loop remain unchanged. Rotation is clockwise.
 */
#include "gui_internal.h"
static EFI_GRAPHICS_OUTPUT_PROTOCOL *physical;
static EFI_GRAPHICS_OUTPUT_PROTOCOL rotated;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE logical_mode;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION logical_info;
static UINT32 *portrait_buffer;
static gui_state_t *frame_owner;
static UINTN physical_width, physical_height;
static EFI_ABSOLUTE_POINTER_PROTOCOL *physical_pointer;
static EFI_ABSOLUTE_POINTER_PROTOCOL logical_pointer;
static EFI_ABSOLUTE_POINTER_MODE logical_pointer_mode;

/* Only the first CPU Architectural Protocol member is needed. Its signature
 * and flush type follow MdePkg/Include/Protocol/Cpu.h in EDK2. Never skip cache
 * synchronization: some nabu firmware framebuffers are not DMA coherent. */
typedef struct nabu_cpu_cache nabu_cpu_cache_t;
struct nabu_cpu_cache {
    EFI_STATUS (EFIAPI *FlushDataCache)(nabu_cpu_cache_t *self,
        EFI_PHYSICAL_ADDRESS start, UINT64 length, UINT32 flush_type);
};
static nabu_cpu_cache_t *cpu_cache;
static UINT32 *direct_buffer;
static UINTN direct_stride;
static int direct_swap;

static EFI_STATUS EFIAPI landscape_pointer(EFI_ABSOLUTE_POINTER_PROTOCOL *self,
                                           EFI_ABSOLUTE_POINTER_STATE *out) {
    (void)self;
    EFI_STATUS status=physical_pointer->GetState(physical_pointer,out);
    if(!EFI_ERROR(status)) {
        UINT64 x=out->CurrentX;
        out->CurrentX=out->CurrentY;
        out->CurrentY=physical_pointer->Mode->AbsoluteMaxX-
                      (x-physical_pointer->Mode->AbsoluteMinX);
    }
    return status;
}

static inline UINT32 fb_pixel(UINT32 p, int swap) {
    return swap ? (p & 0xFF00FF00u) | ((p >> 16) & 0xFF) |
                  ((p & 0xFF) << 16) : p;
}

/* Bounded tiles keep source rows in cache. Four pixels are written together
 * along a destination row, without multiplication in the pixel loop. This
 * also works for unaligned rectangles and Visor's partial horizontal bands. */
static void rotate_rect(UINT32 *dst, UINTN dst_stride, const UINT32 *src,
    UINTN src_stride, UINTN sx, UINTN sy, UINTN dx, UINTN dy,
    UINTN w, UINTN h, int swap) {
    for (UINTN bx=0; bx<w; bx+=32) {
        UINTN ex=w-bx>32 ? bx+32 : w;
        for (UINTN by=0; by<h; by+=32) {
            UINTN ey=h-by>32 ? by+32 : h;
            for (UINTN x=bx; x<ex; x++) {
                const UINT32 *s=src+(sy+by)*src_stride+sx+x;
                UINT32 *d=dst+(dx+x)*dst_stride+physical_width-1-(dy+by);
                UINTN y=by;
                while (ey-y>=4) {
                    d[0]=fb_pixel(s[0],swap);
                    d[-1]=fb_pixel(s[src_stride],swap);
                    d[-2]=fb_pixel(s[src_stride*2],swap);
                    d[-3]=fb_pixel(s[src_stride*3],swap);
                    y+=4;
                    /* Do not form a pointer before the destination allocation
                     * when this tile ends at the left edge of the screen. */
                    if (y<ey) { s+=src_stride*4; d-=4; }
                }
                while (y<ey) {
                    *d=fb_pixel(*s,swap);
                    if (++y<ey) { s+=src_stride; d--; }
                }
            }
        }
    }
}

/* Paint directly into the physical buffer layout. The logical nearest-neighbor
 * maps and dim table are exactly the upstream ones. Repeated source columns
 * copy a completed tile row; repeated source rows reuse the dimmed pixel. */
int nabu_draw_anim_background(gui_state_t *state, const UINT32 *pixels,
    const UINTN *xmap, const UINTN *ymap, const UINT8 *dim, UINT32 fill) {
    if (!state->nabu_portrait_buffer || state!=frame_owner ||
        !state->backbuffer || !pixels || !xmap || !ymap) return 0;
    UINTN W=state->screen_width,H=state->screen_height;
    UINT32 *dst=state->backbuffer;
    for (UINTN bx=0;bx<W;bx+=32) {
        UINTN ex=W-bx>32 ? bx+32 : W;
        for (UINTN by=0;by<H;by+=32) {
            UINTN ey=H-by>32 ? by+32 : H;
            for (UINTN x=bx;x<ex;x++) {
                UINT32 *d=dst+x*H;
                if (x && xmap[x]==xmap[x-1]) {
                    CopyMem(d+H-ey,d-H+H-ey,(ey-by)*sizeof(UINT32));
                    continue;
                }
                UINTN last_row=(UINTN)-1;
                UINT32 value=fill;
                for (UINTN y=by;y<ey;y++) {
                    if (ymap[y]!=last_row) {
                        UINT32 p=pixels[ymap[y]+xmap[x]];
                        value=(p&0xFF000000u) ? 0xFF000000u |
                            ((UINT32)dim[(p>>16)&255]<<16) |
                            ((UINT32)dim[(p>>8)&255]<<8) | dim[p&255] : fill;
                        last_row=ymap[y];
                    }
                    d[H-1-y]=value;
                }
            }
        }
    }
    return 1;
}

/* Screenshot and interactive GIF recording keep the original landscape
 * row-major encoder input. Fades/crossfades remain valid in either layout. */
EFI_STATUS nabu_make_logical_buffer(gui_state_t *state) {
    if (!state->nabu_portrait_buffer) return EFI_SUCCESS;
    UINTN W=state->screen_width,H=state->screen_height,bytes=W*H*4;
    UINT32 *tmp=efi_allocate_pool(bytes);
    if (!tmp) return EFI_OUT_OF_RESOURCES;
    UINT32 *buffers[2]={state->backbuffer,
                       state->scene_valid ? state->scene_cache : NULL};
    for (UINTN b=0;b<2;b++) {
        if (!buffers[b]) continue;
        for (UINTN y=0;y<H;y++) for (UINTN x=0;x<W;x++)
            tmp[y*W+x]=buffers[b][x*H+H-1-y];
        CopyMem(buffers[b],tmp,bytes);
    }
    efi_free_pool(tmp);
    state->nabu_portrait_buffer=0;
    return EFI_SUCCESS;
}

static void enable_direct_buffer(void) {
    static EFI_GUID cpu_guid={0x26baccb1,0x6f42,0x11d4,
        {0xbc,0xe7,0x00,0x80,0xc7,0x3c,0x88,0x81}};
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *m=physical->Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info=m->Info;
    UINTN max=(UINTN)-1, pitch=info->PixelsPerScanLine;
    if ((info->PixelFormat!=PixelBlueGreenRedReserved8BitPerColor &&
         info->PixelFormat!=PixelRedGreenBlueReserved8BitPerColor) ||
        !m->FrameBufferBase || m->FrameBufferBase%4 ||
        m->FrameBufferBase>max || m->FrameBufferSize>max-m->FrameBufferBase ||
        pitch<physical_width || pitch>max/4 ||
        physical_height>m->FrameBufferSize/(pitch*4)) return;
    if (EFI_ERROR(BS->LocateProtocol(&cpu_guid,NULL,(void**)&cpu_cache)) ||
        !cpu_cache || !cpu_cache->FlushDataCache) { cpu_cache=NULL; return; }
    /* WriteBackInvalidate (0) is the mandatory PI flush operation. Preflight
     * it before writing any pixels; unavailable implementations keep GOP Blt. */
    if (EFI_ERROR(cpu_cache->FlushDataCache(cpu_cache,m->FrameBufferBase,
                                           m->FrameBufferSize,0))) {
        cpu_cache=NULL; return;
    }
    direct_buffer=(UINT32*)(UINTN)m->FrameBufferBase;
    direct_stride=pitch;
    direct_swap=info->PixelFormat==PixelRedGreenBlueReserved8BitPerColor;
}

static EFI_STATUS EFIAPI landscape_blt(EFI_GRAPHICS_OUTPUT_PROTOCOL *self,
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *buffer, EFI_GRAPHICS_OUTPUT_BLT_OPERATION op,
    UINTN sx, UINTN sy, UINTN dx, UINTN dy, UINTN w, UINTN h, UINTN delta) {
    (void)self;
    if (op!=EfiBltBufferToVideo) return EFI_UNSUPPORTED;
    if (!buffer || !w || !h || dx>=physical_height || dy>=physical_width ||
        w>physical_height-dx || h>physical_width-dy || (delta && delta%4))
        return EFI_INVALID_PARAMETER;
    UINTN stride=delta ? delta/4 : w, max=(UINTN)-1;
    if (sx>stride || w>stride-sx || sy>max-h || sx+w>max/4 ||
        sy+h-1>(max/4-sx-w)/stride) return EFI_INVALID_PARAMETER;
    UINTN last=((sy+h-1)*stride+sx+w)*4;
    if ((UINTN)buffer>max-last) return EFI_INVALID_PARAMETER;
    const UINT32 *src=(const UINT32*)buffer;
    UINTN px=physical_width-dy-h, py=dx;
    /* The menu already painted in portrait storage: only copy rows, no
     * fullscreen transpose. Other GOP buffers retain the generic path. */
    int pre_rotated=frame_owner && frame_owner->nabu_portrait_buffer &&
                    buffer==(void*)frame_owner->backbuffer;
    UINTN source_px=0;
    if (pre_rotated) {
        if (sx>=physical_height || sy>=physical_width ||
            w>physical_height-sx || h>physical_width-sy)
            return EFI_INVALID_PARAMETER;
        source_px=physical_width-sy-h;
    }
    if (direct_buffer) {
        /* Keep the same TPL protection as the firmware's display driver. */
        EFI_TPL tpl=BS->RaiseTPL(TPL_NOTIFY);
        if (pre_rotated) {
            for (UINTN row=0;row<w;row++) {
                const UINT32 *s=src+(sx+row)*physical_width+source_px;
                UINT32 *d=direct_buffer+(py+row)*direct_stride+px;
                if (!direct_swap) CopyMem(d,s,h*sizeof(UINT32));
                else for (UINTN col=0;col<h;col++) d[col]=fb_pixel(s[col],1);
            }
        } else {
            rotate_rect(direct_buffer,direct_stride,src,stride,sx,sy,dx,dy,w,h,
                        direct_swap);
        }
        UINTN start=py*direct_stride+px;
        UINTN bytes=((w-1)*direct_stride+h)*4;
        EFI_STATUS status=cpu_cache->FlushDataCache(cpu_cache,
            (EFI_PHYSICAL_ADDRESS)(UINTN)(direct_buffer+start),bytes,0);
        BS->RestoreTPL(tpl);
        if (!EFI_ERROR(status)) return EFI_SUCCESS;
        /* A cache failure disables the fast path for this menu session and
         * immediately redraws the requested rectangle through firmware GOP. */
        direct_buffer=NULL;
        efi_log(L"nabu landscape: cache sync failed; using firmware Blt");
    }
    if (pre_rotated)
        return physical->Blt(physical,buffer,EfiBltBufferToVideo,
            source_px,sx,px,py,h,w,physical_width*4);
    if (!portrait_buffer) {
        portrait_buffer=efi_allocate_pool(physical_width*physical_height*4);
        if (!portrait_buffer) return EFI_OUT_OF_RESOURCES;
    }
    rotate_rect(portrait_buffer,physical_width,src,stride,sx,sy,dx,dy,w,h,0);
    return physical->Blt(physical,(EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)portrait_buffer,
        EfiBltBufferToVideo,px,py,px,py,h,w,physical_width*4);
}

EFI_STATUS nabu_landscape_begin(gui_state_t *state) {
    if (state->screen_width>=state->screen_height) return EFI_SUCCESS;
    if (!state->screen_width || state->screen_height>(UINTN)-1/4/state->screen_width)
        return EFI_INVALID_PARAMETER;
    physical=state->gop;
    physical_width=state->screen_width; physical_height=state->screen_height;
    enable_direct_buffer();
    efi_log(direct_buffer ? L"nabu landscape: direct framebuffer + cache sync" :
                           L"nabu landscape: firmware Blt fallback");
    rotated=*physical; logical_mode=*physical->Mode;
    logical_info=*physical->Mode->Info;
    logical_info.HorizontalResolution=(UINT32)physical_height;
    logical_info.VerticalResolution=(UINT32)physical_width;
    logical_info.PixelsPerScanLine=(UINT32)physical_height;
    logical_info.PixelFormat=PixelBltOnly;
    logical_mode.Info=&logical_info;
    logical_mode.FrameBufferBase=0; logical_mode.FrameBufferSize=0;
    rotated.Mode=&logical_mode; rotated.Blt=landscape_blt;
    state->gop=&rotated;
    state->screen_width=physical_height; state->screen_height=physical_width;
    state->pixels_per_scanline=physical_height;
    state->pixel_format=PixelBltOnly; state->fb_fast=0;
    state->cursor_x=state->screen_width/2;state->cursor_y=state->screen_height/2;
    EFI_ABSOLUTE_POINTER_PROTOCOL *ap=state->app;
    if(ap && ap->Mode) {
        physical_pointer=ap;
        logical_pointer=*physical_pointer;logical_pointer_mode=*physical_pointer->Mode;
        logical_pointer_mode.AbsoluteMinX=physical_pointer->Mode->AbsoluteMinY;
        logical_pointer_mode.AbsoluteMaxX=physical_pointer->Mode->AbsoluteMaxY;
        logical_pointer_mode.AbsoluteMinY=physical_pointer->Mode->AbsoluteMinX;
        logical_pointer_mode.AbsoluteMaxY=physical_pointer->Mode->AbsoluteMaxX;
        logical_pointer.Mode=&logical_pointer_mode;logical_pointer.GetState=landscape_pointer;
        state->app=&logical_pointer;
    }
    frame_owner=state;
    /* A whole-boot recorder is armed before gui_init. Keep its existing input
     * layout throughout that recording; no feature is removed. */
    state->nabu_portrait_buffer=!cap_session_active();
    state->scene_valid=0;
    efi_log(state->nabu_portrait_buffer ? L"nabu render: fused portrait storage" :
                                        L"nabu render: logical recording storage");
    return EFI_SUCCESS;
}

void nabu_landscape_end(void) {
    if (portrait_buffer) efi_free_pool(portrait_buffer);
    portrait_buffer=NULL; physical=NULL; physical_pointer=NULL; frame_owner=NULL;
    direct_buffer=NULL; cpu_cache=NULL; direct_stride=0; direct_swap=0;
}
