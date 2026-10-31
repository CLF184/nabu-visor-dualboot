# ESP 应用分支实现

本分支采用 [Issue #1](https://github.com/CLF184/nabu-visor-dualboot/issues/1#issuecomment-6538193058) 的原样 FD、ESP 菜单和 PSCI 交接方案。支持 Secure Boot 的原 FD 内菜单实现保留在 [secure-boot 分支](https://github.com/CLF184/nabu-visor-dualboot/tree/secure-boot)。

## 菜单与固件

build.py 编译 NabuVisorMenu.efi，PE subsystem 为 EFI application（10）。安装在 ESP 的 EFI/BOOT/bootaa64.efi，由原固件 BDS 加载。src/menu.zig 只导出一个 EfiMain，收集 SimpleTextInputEx 并进入 Visor；不查找或替换 BDS 入口、不执行提前 connectAll。

输入 FD 通过 boot_inputs.validate_passthrough_fd 检查外层 FV 的长度、格式、版本、校验和与块映射；允许 FFS2 / FFS3。内部模块作为不透明内容原样保留，不调用 FD 改写器、不重压缩、不查找指定 BDS GUID，不编译或注入 BootSlotDxe。FD 仍必须符合 nabu 的 3 MiB / 0x9FC00000 加载约定，外层格式正确不代表固件硬件兼容。

本分支不修改 Secure Boot 开关或证书，所提供固件必须允许加载这些未签名应用。使用 NoSB 后 Windows 的 Secure Boot 保护也会关闭。没有额外槽位维护；若输入固件缺少该功能，持续只进入 UEFI/其他系统可能耗尽 bootloader 重试次数，应监控当前槽状态并保留回滚手段。

## Android 与重启

Renegade 固定为 e1952621f419f8db60ed28271264e1b5184c571d，原版 Reboot2PayloadApp.c 与 BootShim 扫描/清除循环经 Git blob 校验。NabuAndroid.efi 使用独立 src/android.zig 入口。

写入端仍将 0x17FE00000 的 128 个半字设为 0x5241，按 CTR_EL0 缓存行写回并失效 256 字节。port/renegade/bridge.c 复制 runtime-services 表，仅将传给原版标记程序的私有副本 ResetSystem 指向 PSCI SYSTEM_RESET（SMC 0x84000009，x1–x3 为 0）。不替换全局系统表或固件重启驱动；菜单关机与普通重启仍走固件原接口。若 PSCI 返回错误，沿用上游 CpuDeadLoop。

接收端保持原扫描/清除循环，任意标记匹配即清除 256 字节并跳回 Android 原入口。没有标记则检查原生 Recovery / 关机充电环境，否则按 Nabu2 约定复制并跳转 FD。

该协议依赖底层固件重启后保留标记页。PSCI 的规范接口或编译通过都不能保证某台设备保留 DRAM 内容。

## Recovery 与充电

Recovery 仅保留左下角按钮。菜单初始化 GNU-EFI 全局后，通过 nabu_power_action 调用现有 recovery.c 的 nabu_recovery_boot，复用标记/PSCI 桥接。不生成独立 Recovery EFI 或系统条目。

按 GPT 名找到唯一 misc 逻辑分区，仅更新 command[32] 为 boot-recovery、recovery[768] 为 recovery 换行；status、stage、其他数据及 4 KiB 块尾保留。写入、刷新、读回核对后才写标记并 PSCI 复位。请求写入失败则尝试恢复，不重启；恢复失败保留错误提示。

port/recovery/native_entry.S 继续在 FD 之前验证原厂 FDT：有效 nabu/qcom 启动参数不含 force_normal_boot=1 时直接继续原内核；含完整 androidboot.mode=charger 时优先继续原充电路径。保留 x0–x3，不使用栈，不执行 boot-time BCB 检测或多余重启。

Recovery 的实际硬件支持由所选 Android 内核、设备树、固件和 ramdisk 决定。--recovery 仅替换 ramdisk，不使用原 TWRP 内核；触屏驱动解析不匹配的设备树失败时，不能靠更换 UI 或复制 ramdisk 修好。

## Windows 与配置

Windows 使用 Visor 原版 LoadImage / StartImage 加载配置的 EFI/Microsoft/Boot/bootmgfw.efi，不交还 BDS，也不自动跳到固件默认系统。

Android 使用普通 EFI 条目，kernel 指向配套 NabuAndroid.efi；Recovery 使用左下角按钮。nabu_action 解析仅支持 linux，不再包含 android/bds/windows 的旧动作解析、截获代码或固件内置默认条目。不要继续使用旧版配置与固件热重启 Android 应用。

配置仍统一为 EFI/visor/boot.conf。配置缺失时沿用上游自动发现，不生成固件内置默认条目。默认配置关闭 selfheal，避免自动写 NVRAM 或替换回退 EFI。用户显式启用的上游功能不裁剪。

## 显示、输入与 Linux

保留完整 ARM64 功能清单、横屏适配、音量键选择/电源键松开确认，以及 5 秒无操作自动 Android 的原有逻辑。

横屏缓冲与背景缩放/颜色转换保留主分支已合并的绘制路径，视频解码、帧调度、主题和配置格式不改。提交按物理屏序复制局部区域，必要时通过固件 GOP Blt 回退，截图/录屏仍转换回逻辑横屏。

Linux 的 nabu_action=linux 仍复用原版 rEFInd。生成并回读核对 nabu-selected.conf，保留原 refind.conf 的主题 include，通过 volume/loader/initrd/options 交接。rEFInd 的目录独立于主菜单 EFI/BOOT。

本分支运行时原 BDS 已结束 DXE 初始化，不重复发送 EndOfDxe。使用固件已提供的系统表，不伪造 Project Silicium 厂商名；rEFInd 所需厂商字段缺失时返回错误，避免比较空指针。既有字段、版本及校验和不改。

保留 Linux 动作对加密/固定哈希等无法转交约束的拒绝行为，普通 Visor 条目的功能仍保留。Visor、Linux 交接、rEFInd 的文件日志默认关闭。

## 构建、安装与更新

boot_inputs / root_patch 的可选 Recovery、Magisk、KSU 处理保持主分支语义；输入不修改。镜像补丁校验原 Android payload、FD 追加位置、原厂入口和 ramdisk 重打包一致性。构建临时目录位于项目 build 下；成品和展开 ESP 发布到所选输出目录，失败回滚，不回写工程 esp 模板。

安装不仅要复制 EFI/visor，还要备份并替换 EFI/BOOT/bootaa64.efi；微软引导文件保持原样。Windows 安装脚本从默认 dist/esp 复制，最后替换回退 EFI。回滚先恢复原回退 EFI 和 boot，再移除 Visor 资源。

所有需要安装到 ESP 的项目文件都包含在 nabu-visor-esp.zip，目录以 EFI 开始；菜单副本按 EFI/BOOT/bootaa64.efi 命名，Android 程序、配置、主题及准备好的 rEFInd 资源位于 EFI/visor。顶层两个 EFI 文件是参考副本，不需要在 ZIP 之外额外安装。发布前核对关键路径、字节内容和整个 ZIP 的 CRC，manifest 记录安装方式和已核对的文件。

一键更新必须显式提供 NoSB --fd，仍在临时上游副本应用 NABU.patch。菜单与 Android 应用分别编译，没有共享的 EfiMain 入口；Recovery 处理链接在菜单内。适配维护在主仓库补丁/桥接中，不改子模块工作区。

旧 boot_slot_build.py、port/boot-slot 等来源记录保留用于对照，不参与本分支的 build.py；不能据此宣称新镜像有 BootSlotDxe 保护。

## 校验

tests/test_esp_application.py 检查 FFS2/FFS3 原样提取、内部 BDS 不依赖、错误头部/块映射拒绝。test_boot_inputs、test_patch_boot、test_build_publish 检查输入保护、FD 对齐、原始分流约定和发布回滚；test_linux_system.c 检查原 BDS 元数据保持不变、缺失字段拒绝且不写表。

成品 manifest 记录真实的输入 FD 哈希、整个 FD 是否未改、PSCI 桥接、外部菜单路径与硬件验证状态。主机检查不代替 Android、Windows、Recovery、Linux 和重启标记保留的实机验证。
