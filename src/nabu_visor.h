/* SPDX-License-Identifier: MIT */
#ifndef NABU_VISOR_H
#define NABU_VISOR_H
#include "config.h"

/* 条目动作（boot_entry_t.nabu_action / boot.conf 键 nabu_action） */
#define NABU_ACTION_NONE    0
#define NABU_ACTION_BDS     1  /* 交回固件原始 BDS：Windows 按原生流程启动 */
#define NABU_ACTION_LINUX   3  /* 原版 rEFInd 处理选中的 Linux EFI 条目 */
#define NABU_ACTION_ANDROID 2  /* 写标记 + 热重启：内核分流代码回到 Android */

/* 由 Visor 主流程在 Android/BDS 动作被选中时写入；
 * 固件卷内驱动（src/menu.zig）在 nabu_visor_menu() 返回后读取并执行。 */
extern int nabu_exit_action;

EFI_STATUS nabu_linux_table_begin(EFI_BOOT_SERVICES *bs, EFI_HANDLE image);
void nabu_linux_table_end(void);
void nabu_linux_trace_begin(EFI_HANDLE volume, EFI_SYSTEM_TABLE *st);
void nabu_linux_trace_end(void);
void nabu_linux_trace(const char *stage, EFI_STATUS status);
EFI_STATUS nabu_linux_load_image(EFI_BOOT_SERVICES *bs, BOOLEAN policy,
    EFI_HANDLE parent, EFI_DEVICE_PATH *path, VOID *buffer, UINTN size,
    EFI_HANDLE *image);
EFI_STATUS nabu_linux_start_image(EFI_BOOT_SERVICES *bs, EFI_HANDLE image,
    UINTN *exit_size, CHAR16 **exit_data);

EFI_STATUS nabu_boot_linux(boot_entry_t *entry, EFI_SYSTEM_TABLE *st);
EFI_STATUS nabu_gui_ready(gui_state_t *gui,int text_mode);
EFI_STATUS nabu_power_action(int action);
#endif
