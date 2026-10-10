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
static UINTN physical_width, physical_height;
static EFI_ABSOLUTE_POINTER_PROTOCOL *physical_pointer;
static EFI_ABSOLUTE_POINTER_PROTOCOL logical_pointer;
static EFI_ABSOLUTE_POINTER_MODE logical_pointer_mode;

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

static EFI_STATUS EFIAPI landscape_blt(EFI_GRAPHICS_OUTPUT_PROTOCOL *self,
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *buffer, EFI_GRAPHICS_OUTPUT_BLT_OPERATION op,
    UINTN sx, UINTN sy, UINTN dx, UINTN dy, UINTN w, UINTN h, UINTN delta) {
    (void)self;
    if (op != EfiBltBufferToVideo) return EFI_UNSUPPORTED;
    if (!buffer || !w || !h || dx >= physical_height || dy >= physical_width ||
        w > physical_height-dx || h > physical_width-dy || (delta && delta%4))
        return EFI_INVALID_PARAMETER;
    UINTN stride = delta ? delta/4 : w;
    if (sx > stride || w > stride-sx) return EFI_INVALID_PARAMETER;
    const UINT32 *src = (const UINT32*)buffer;
    /* Tile the transpose for local framebuffer writes, including Visor's
     * partial horizontal-band updates. */
    for (UINTN by=0; by<h; by+=16) for (UINTN bx=0; bx<w; bx+=16) {
        UINTN ey=by+16<h?by+16:h, ex=bx+16<w?bx+16:w;
        for (UINTN x=bx; x<ex; x++) for (UINTN y=by; y<ey; y++)
            portrait_buffer[(dx+x)*physical_width + physical_width-1-(dy+y)] =
                src[(sy+y)*stride + sx+x];
    }
    UINTN px=physical_width-dy-h, py=dx;
    return physical->Blt(physical, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)portrait_buffer,
        EfiBltBufferToVideo, px, py, px, py, h, w, physical_width*4);
}

EFI_STATUS nabu_landscape_begin(gui_state_t *state) {
    if (state->screen_width >= state->screen_height) return EFI_SUCCESS;
    physical = state->gop;
    physical_width = state->screen_width; physical_height = state->screen_height;
    portrait_buffer = efi_allocate_pool(physical_width*physical_height*4);
    if (!portrait_buffer) return EFI_OUT_OF_RESOURCES;
    ZeroMem(portrait_buffer, physical_width*physical_height*4);
    rotated = *physical; logical_mode = *physical->Mode;
    logical_info = *physical->Mode->Info;
    logical_info.HorizontalResolution = (UINT32)physical_height;
    logical_info.VerticalResolution = (UINT32)physical_width;
    logical_info.PixelsPerScanLine = (UINT32)physical_height;
    logical_info.PixelFormat = PixelBltOnly;
    logical_mode.Info = &logical_info;
    logical_mode.FrameBufferBase = 0; logical_mode.FrameBufferSize = 0;
    rotated.Mode = &logical_mode; rotated.Blt = landscape_blt;
    state->gop = &rotated;
    state->screen_width = physical_height; state->screen_height = physical_width;
    state->pixels_per_scanline = physical_height;
    state->pixel_format = PixelBltOnly; state->fb_fast = 0;
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
    state->scene_valid = 0;
    return EFI_SUCCESS;
}

void nabu_landscape_end(void) {
    if (portrait_buffer) efi_free_pool(portrait_buffer);
    portrait_buffer = NULL; physical = NULL;physical_pointer=NULL;
}
