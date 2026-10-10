/* SPDX-License-Identifier: MIT
 * Hardware glue for upstream Visor; configuration and resources live on ESP.
 */
#include "gui_internal.h"
#include "config.h"
#include "nabu_visor.h"
_Static_assert(sizeof(EFI_INPUT_KEY) == 4, "EFI key ABI");
_Static_assert(sizeof(EFI_SYSTEM_TABLE) == 120, "64-bit EFI system table ABI");

extern EFI_STATUS NabuReadKey(EFI_INPUT_KEY *key);
extern void nabu_font_release(void);
extern EFI_STATUS nabu_landscape_begin(gui_state_t *state);
extern void nabu_landscape_end(void);
static EFI_SYSTEM_TABLE local_table;
static SIMPLE_INPUT_INTERFACE local_input;
static UINT64 last_poll;
static int first_poll;
static gui_state_t *active_state;
/* Visor 主流程选择 nabu_action 条目时写入；驱动读取后执行对应动作。 */
int nabu_exit_action = NABU_ACTION_NONE;
#ifndef VISOR_HOST_TEST
static UINT64 frequency;
void arch_clock_init(void) {
    if (!frequency) __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
}
UINT64 arch_now_us(void) {
    UINT64 ticks;
    arch_clock_init();
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(ticks));
    return frequency ? (ticks / frequency * 1000000 + ticks % frequency * 1000000 / frequency) : 0;
}
#endif
const CHAR16 *arch_fb_make_wc(UINT64 base, UINT64 size) {
    (void)base; (void)size; return L"GOP";
}
int arch_clock_since_power_on(void) { return 1; }

static EFI_STATUS EFIAPI read_input(SIMPLE_INPUT_INTERFACE *self, EFI_INPUT_KEY *key) {
    (void)self;
    UINT64 now = efi_get_tick();
    if (!first_poll && now - last_poll < 50) return EFI_NOT_READY;
    first_poll = 0; last_poll = now;
    EFI_STATUS status = NabuReadKey(key);
    if (!EFI_ERROR(status) && active_state && active_state->backbuffer && active_state->entry_count && !active_state->editing &&
        !active_state->version_mode && !active_state->snap_mode && !active_state->browse && key->UnicodeChar==0 &&
        (key->ScanCode==1 || key->ScanCode==2)) {
        /* Extend a two-button device to Visor's native entry/power focus.
         * Feed one normal arrow to its unchanged event loop. The staging
         * state is never painted; Visor applies the arrow before redrawing. */
        gui_state_t *s=active_state;
        UINTN n=s->entry_count, count=n+POWER_ACTION_COUNT;
        UINTN from=s->focus==FOCUS_POWER?n+s->power_sel:s->selected;
        UINTN to=(from + (key->ScanCode==2?1:count-1))%count;
        if (to<n) {
            s->focus=FOCUS_ENTRIES;
            s->selected=to==0?n-1:to-1;
            key->ScanCode=3;
        } else {
            s->focus=FOCUS_POWER;
            UINTN power=to-n;
            if(power==0){s->power_sel=1;key->ScanCode=1;}
            else{s->power_sel=power-1;key->ScanCode=2;}
        }
    }
    return status;
}

/* Qualcomm ShutdownServices.h defines a packed 25-byte reset parameter:
 * CHAR16 DataBuffer[12] = L"RESET_PARAM", UINT8 Bdata = FASTBOOT_MODE (2).
 * ShutdownServices.c passes EFI_INVALID_PARAMETER for non-normal reasons.
 * No BCB writes, raw PMIC accesses or partition modifications are needed. */
EFI_STATUS nabu_power_action(int action) {
    if(!RT || !RT->ResetSystem)return EFI_UNSUPPORTED;
    if(action==VISOR_ACTION_FIRMWARE) {
        EFI_GUID reason_guid={0xa022155a,0x4828,0x4535,{0xa4,0x99,0x11,0xf1,0x52,0x40,0xb9,0x1b}};
        void *reason=NULL;
        if(EFI_ERROR(BS->LocateProtocol(&reason_guid,NULL,&reason)) || !reason)return EFI_UNSUPPORTED;
        UINT8 data[25]={0};
        const CHAR16 *tag=L"RESET_PARAM";
        for(UINTN i=0;i<11;i++){data[i*2]=(UINT8)tag[i];data[i*2+1]=(UINT8)(tag[i]>>8);}
        data[24]=2;
        RT->ResetSystem(EfiResetCold,EFI_INVALID_PARAMETER,sizeof(data),data);
        return EFI_DEVICE_ERROR;
    }
    RT->ResetSystem(action==VISOR_ACTION_SHUTDOWN?EfiResetShutdown:EfiResetCold,EFI_SUCCESS,0,NULL);
    return EFI_DEVICE_ERROR; /* EFI reset must not return. */
}

EFI_STATUS nabu_gui_ready(gui_state_t *gui,int text_mode) {
    active_state=gui;
    if(text_mode)return EFI_SUCCESS;
    EFI_STATUS status=nabu_landscape_begin(gui);if(EFI_ERROR(status))return status;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI reset_input(SIMPLE_INPUT_INTERFACE *self,BOOLEAN verify) {
    (void)self;(void)verify;return EFI_SUCCESS;
}
extern EFI_STATUS efi_main(EFI_HANDLE handle,EFI_SYSTEM_TABLE *table);
void nabu_visor_menu(EFI_HANDLE handle,EFI_SYSTEM_TABLE *table) {
    local_table=*table;ZeroMem(&local_input,sizeof(local_input));
    if(table->ConIn)local_input=*table->ConIn;
    local_input.ReadKeyStroke=read_input;local_input.Reset=reset_input;
    local_table.ConIn=&local_input;
    first_poll=1;last_poll=0;
    efi_main(handle,&local_table);
    efi_log_close();active_state=NULL;nabu_font_release();nabu_landscape_end();
}
