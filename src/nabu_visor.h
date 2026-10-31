/* SPDX-License-Identifier: MIT */
#ifndef NABU_VISOR_H
#define NABU_VISOR_H
#include "config.h"

/* 条目动作（boot_entry_t.nabu_action / boot.conf 键 nabu_action） */
#define NABU_ACTION_NONE    0
#define NABU_ACTION_LINUX   3  /* 原版 rEFInd 处理选中的 Linux EFI 条目 */

EFI_STATUS nabu_linux_table_begin(EFI_BOOT_SERVICES *bs, EFI_HANDLE image);
void nabu_linux_handoff_begin(void);
void nabu_linux_handoff_end(void);
EFI_STATUS nabu_linux_start_image(EFI_BOOT_SERVICES *bs, EFI_HANDLE image,
    UINTN *exit_size, CHAR16 **exit_data);

EFI_STATUS nabu_boot_linux(boot_entry_t *entry, EFI_SYSTEM_TABLE *st);
EFI_STATUS nabu_gui_ready(gui_state_t *gui,int text_mode);
EFI_STATUS nabu_power_action(int action);
#endif
