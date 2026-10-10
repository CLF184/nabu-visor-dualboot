/* SPDX-License-Identifier: MIT */
#ifndef NABU_VISOR_H
#define NABU_VISOR_H
#include "config.h"

/* 条目动作（boot_entry_t.nabu_action / boot.conf 键 nabu_action） */
#define NABU_ACTION_NONE    0
#define NABU_ACTION_BDS     1  /* 交回固件原始 BDS：Windows 按原生流程启动 */
#define NABU_ACTION_ANDROID 2  /* 写标记 + 热重启：内核分流代码回到 Android */

/* 由 Visor 主流程在 nabu_action 条目被选中时写入；
 * 固件卷内驱动（src/menu.zig）在 nabu_visor_menu() 返回后读取并执行。 */
extern int nabu_exit_action;

EFI_STATUS nabu_gui_ready(gui_state_t *gui,int text_mode);
EFI_STATUS nabu_power_action(int action);
#endif
