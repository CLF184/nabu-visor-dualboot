# 当前 nabu 移植实现

本机已有反馈：Android 标记配对方案成功；Windows 原始 BDS 流程成功；用户于 2026-10-10 确认 Recovery 按钮可直接进入 Recovery。Linux 尚未在本机验证。

## 菜单与固件

Visor 编译为 AArch64 DXE 驱动 `NabuBootMenu.efi`，插入基础 FD，挂接原 BDS 入口并在其前显示菜单。配置统一从 ESP 的 `\EFI\visor\boot.conf` 读取，使用上游 `CONFIG_FILE` 常量；图标、背景继续从 ESP 的 `\EFI\visor\` 加载。基础 FD 的其他可执行模块保留原字节。

横屏、音量键轮流选择系统和电源动作、电源松开确认是 nabu 适配。初始 5 秒无输入自动 Android，输入后取消本次倒计时。构建按上游完整功能清单处理，只遵循其 ARM64 架构限制。

## Android 标记配对

Renegade 源码固定为 `e1952621f419f8db60ed28271264e1b5184c571d`。上游 `Reboot2PayloadApp.c` 从 `third_party/renegade/` 子模块直接读取，构建校验 Git blob。

写入端将 `0x17FE00000` 的 128 个半字设为 `0x5241`，写回并失效全部 256 字节缓存，调用 `EfiResetWarm`。EDK2 到 GNU-EFI 的适配只有运行时表和库函数桥接；缓存行大小从 `CTR_EL0` 读取。

接收端沿用上游 `BootShim.Dualboot.S` 的扫描和清除循环，将 76 字节接收代码追加在 FD 后。任意标记匹配时，清空全部 256 字节，再跳回原内核；未匹配时接回官方 Nabu2 的 FD 复制入口。原 Android 内核 `0xb0` 后的主体、原 FD 入口约定和解压后的 ramdisk 保留。

标记表示“下一次绕过菜单进入原内核”，并不单独指定 Android 或 Recovery；具体启动目标还取决于原厂 bootloader 准备的启动模式。

## Recovery

`port/recovery/recovery.c` 按 GPT 分区名找到唯一 `misc` 逻辑分区，更新 AOSP BCB：

| 字段 | 偏移/长度 | 请求值 |
| --- | --- | --- |
| command | 0 / 32 字节 | `boot-recovery` |
| recovery | 64 / 768 字节 | `recovery\n` |

仅这两个字段改变。512 字节块读写开头 2048 字节；4096 字节块读写完整 4096 字节并保留其他内容。写入、刷新和读回核对通过后，调用已经验证的 `nabu_renegade_android` 桥接函数，写一次性标记并热重启。因此 Recovery 启动不会再次停在 Visor。

请求写入失败时不写标记、不重启，并尝试恢复原始 BCB。若恢复也失败，显示对应提示。沿用的上游标记程序在 ResetSystem 异常返回时进入 `CpuDeadLoop`。

按钮进入平板已经安装的 Recovery，不直接加载 Linux 内核，不写入提供过的 TWRP 文件，也不更换固件重启驱动。

## Windows 与 Linux

Windows 的动作条目 `nabu_action=bds` 干净退出 Visor，再执行原固件 BDS。配置中的 Windows 路径不作为这条动作的实际 LoadImage 目标。因此当前行为跟随固件正常默认启动流程。

普通条目不写 `nabu_action`，继续使用上游 Visor 的 EFI 加载器。Linux 的 ARM64 EFI 程序或 EFI Stub 内核可走这条路径，但本机兼容性还未验证。三系统需要独立核对 Windows 与 Linux 引导入口。

## 构建覆盖与检查

`src/` 是基础平台层。构建先在临时目录复制它，并使用 `port/renegade/nabu_reset.zig` 替换副本中的 Android backend。`port/recovery/overlay.py` 只生成以下三个覆盖文件：

1. `src/visor_platform.c`：第三按钮调用 Recovery 请求。
2. `third_party/visor/src/gui/gui_panels.c`：标签为 Recovery，保留原图标。
3. `third_party/visor/src/core/main.c`：请求失败显示原因并取消自动倒计时。

构建校验上游源码、固件模块保留、原内核主体和 ramdisk 保留，以及打包后内核匹配。模拟存储检查覆盖 BCB 修改范围、512/4096 字节块、对齐、失败恢复和调用标记程序的先后顺序。已有真机结果只适用于已交付镜像；新输入生成的镜像需要重新确认。

输出目录可以存在，同名生成文件覆盖。临时构建目录每次重新创建，避免旧解包文件影响新构建。

## 子模块与构建副本

三个参与编译的依赖作为 Git 子模块固定版本。Visor 的完整 nabu 补丁在 `port/visor/NABU.patch`。构建复制 Visor 到临时目录，规范补丁目标的文本换行后应用补丁，再应用 Recovery 覆盖器；不修改子模块工作树。Renegade 原版程序直接从子模块读取，源码校验按规范 LF 字节计算 Git blob，另记录实际工作树 SHA256。

首次克隆时，功能头文件与清单自动生成，默认外部资源从 Visor 子模块准备到 ESP 副本，配套 Android EFI 程序在构建中生成。编译器、原始输入、工具可执行文件及输出镜像不提交。
