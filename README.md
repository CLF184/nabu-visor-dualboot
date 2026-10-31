# nabu Visor 多系统启动菜单

**推荐使用当前默认分支 `esp-application`。** 本分支参考 [Issue #1 的修复方案](https://github.com/CLF184/nabu-visor-dualboot/issues/1#issuecomment-6538193058)：FD 保持原样，由原固件从 ESP 加载 Visor。Android、Windows、Recovery 使用独立 EFI 启动项，Linux 保留 rEFInd 接入。

**需要允许未签名 EFI 应用的 nabu NoSB 固件，Windows 的 Secure Boot 保护也会关闭。** 工具不会自动关闭 Secure Boot、修改证书或把普通固件转换为 NoSB。

需要 Windows Secure Boot 时，请使用 [secure-boot 分支](https://github.com/CLF184/nabu-visor-dualboot/tree/secure-boot)。该分支向 FD 加入菜单驱动、挂接原 BDS，依赖固件实现；其他来源 FD 可能黑屏、卡启动画面或交接崩溃。构建成功不能证明固件兼容，须保留原 boot 和 ESP 备份，准备 Fastboot 回滚。

## 启动方式

```text
ABL → 多系统 boot 的入口分流器
      ├ Android 标记 / 原厂 Recovery / 关机充电 → 原 Android 内核
      └ 原样 NoSB UEFI → 原 BDS → EFI/BOOT/bootaa64.efi（Visor）
                                 ├ Android → NabuAndroid.efi → 标记＋PSCI 重启
                                 ├ Windows → 微软 bootmgfw.efi
                                 ├ Recovery → NabuRecovery.efi → BCB＋标记＋PSCI 重启
                                 └ Linux → 原版 rEFInd → 所选 EFI/内核
```

- 音量上/下选择，电源键松开确认；首次进入后 5 秒无输入自动选择 Android，有输入就取消本次倒计时。
- 保留 Visor 完整 ARM64 功能清单、横屏、外部主题、视频背景、文件浏览和普通 EFI 加载。
- 菜单位于 ESP，更新菜单程序、主题和配置无需刷入 boot。
- FD 不注入菜单、重启驱动或 BootSlotDxe，不挂接 BDS。已有模块原样保留；没有额外的 A/B 成功标记和重试次数维护，相关行为由输入固件与系统负责。
- PSCI 重启是否保留内存标记取决于底层固件；原生 Recovery 和关机充电仍按 nabu FDT 启动参数识别。
- 仅适用于 nabu/SM8150 的 3 MiB FD、固定加载地址和启动协议，不能直接用于其他设备。

## 准备与构建

Windows PowerShell / CMD：

```cmd
git clone --branch esp-application https://github.com/CLF184/nabu-visor-dualboot.git
cd nabu-visor-dualboot
git submodule update --init --depth 1
py -m pip install --target tools/python ziglang==0.14.1
py build.py --boot "E:\path\boot.img" --fd "E:\path\nabu-NoSB.img" --out-dir ".\dist"
```

需要 Python 3、Git、Zig 0.14.1，以及未加本菜单补丁的 Android boot 和匹配设备的 NoSB UEFI。三个子模块为 Visor、GNU-EFI、Renegade；Windows 补丁工具已随仓库提供，见 [来源与许可](tools/official/README.md)。

`--fd` 必须显式指定，支持原始 .fd 或 UEFI Android boot .img。也可用 [ArKT 的 FD 提取网页](https://arkt-7.github.io/nabu-uefi-autopatcher/) 提取。构建校验外层 FFS2/FFS3 固件卷和唯一 3 MiB FD，提取后原样附加，不重建内部固件卷；这不等于验证了硬件兼容性或 NoSB 状态。

输出目录可以已存在；同名文件在检查通过后覆盖，其他文件保留。旧输出保存在 .build-backup/backup.zip。产物仅发布到输出目录，不回写工程 esp 模板，也不操作平板。

### 可选 Recovery 与 root

不指定对应文件时，保留输入 boot 的原有内容，不执行该项修补：

| 参数 | 行为 |
| --- | --- |
| --recovery | 整体替换 Recovery ramdisk，保留 Android 内核 |
| --magisk-apk / --ksu-apk | root APK 二选一，也可全部省略 |
| --ksu-kmi | 可选 KSU KMI，仍须匹配输入内核 |

```cmd
py build.py --boot "E:\path\boot.img" --fd "E:\path\nabu-NoSB.img" --recovery "E:\path\twrp.img" --magisk-apk "E:\path\Magisk.apk"
```

处理顺序为 Recovery ramdisk → root → 分流器与原样 FD，全程 Windows 离线。只选择 Recovery 不迁移旧 ramdisk 的 root；Recovery 必须与保留的 Android 内核、设备树和固件兼容，不能保证任意组合的触屏等硬件可用。Magisk 执行 APK 原脚本；KSU 使用 APK 自带模块/init 和原版 Windows ksud，不兼容时报错。常见 nabu 4.14 不满足官方 KSU LKM 条件。详见 [实现说明](docs/IMPLEMENTATION.md)。

### 成品

| 文件 | 用途 |
| --- | --- |
| nabu-visor-boot.img | Android 内核、入口分流器和原样 FD |
| nabu-visor.fd | 与提取到的输入 FD 完全相同 |
| NabuVisorMenu.efi | 独立菜单，安装为 EFI/BOOT/bootaa64.efi |
| NabuAndroid.efi / NabuRecovery.efi | 独立 Android / Recovery 应用 |
| nabu-visor-esp.zip、esp/ | 完整 ESP 包及展开目录 |
| RenegadeMarkerLoops.bin | 标记接收、原厂 Recovery / 关机充电识别 |
| manifest.json / SHA256SUMS.txt | 来源、交接方式、检查结果和哈希 |

三个程序均为 EFI application，每个 Zig 入口文件只导出一个 EfiMain。不生成菜单 DXE 或 BootSlotDxe。

## 安装与回滚

先备份现有 boot、ESP 的 EFI/BOOT/bootaa64.efi 和个人 EFI/visor 配置，确认 EFI/Microsoft/Boot/bootmgfw.efi 是微软引导程序。

1. 解压 ESP 包，将 **EFI/visor 及 EFI/BOOT/bootaa64.efi 都复制到平板 ESP**。仅复制旧版配置目录不会安装新菜单。
2. 保留微软和 Linux EFI，Windows 不能指回菜单本身。不要通过覆盖微软 bootmgfw.efi 安装菜单。
3. 核对当前槽位后刷入 boot。以下以 A 槽为例：

从旧版固件内菜单切换时，需要同时安装新 ESP 包并刷入含原样 NoSB FD 的新 boot；仅替换 EFI 不会移除旧 FD 内的菜单驱动。

```cmd
fastboot getvar current-slot
fastboot flash boot_a .\dist\nabu-visor-boot.img
fastboot reboot
```

固件须把 EFI/BOOT/bootaa64.efi 设为启动目标；若优先加载其他 EFI，需在固件中选择此路径。

也可在**平板 Windows 上**以管理员权限运行 install/install-windows.cmd，从默认 dist/esp 安装。脚本保留原回退 EFI 为 EFI/BOOT/bootaa64.efi.before-visor，再复制菜单。不要在构建电脑上运行。

回滚须刷回原 boot，并恢复原 EFI/BOOT/bootaa64.efi。卸载脚本先恢复该备份，再移除 Visor 资源；缺少备份时停止，需手动恢复。仅刷回 boot 不会恢复 ESP 文件。

## 配置与主题

仍读取平板 ESP 的 **\EFI\visor\boot.conf**。Android、Windows、Recovery 使用真实 EFI 路径，不再配置 nabu_action=android / bds：

```ini
file_log=0
timeout=5
default=0
remember_last=0
autoboot=0
selfheal=0
title=none
background=\EFI\visor\backgrounds\default.png

entry {
    name=Android
    kernel=\EFI\visor\NabuAndroid.efi
}
entry {
    name=Windows
    kernel=\EFI\Microsoft\Boot\bootmgfw.efi
}
entry {
    name=Recovery
    kernel=\EFI\visor\NabuRecovery.efi
}
```

旧配置的 android/bds 值不再截获启动，使用真实 kernel 路径；**必须更新配套 Android EFI，不能继续用旧版热重启程序**。标题、背景、图标及字体沿用 Visor 格式，完整语法见 boot.conf.example。

配置包使用工程 esp/EFI/visor/boot.conf；修改 config/boot.conf 后可先同步再构建：

```powershell
Copy-Item .\config\boot.conf .\esp\EFI\visor\boot.conf
```

[三系统参考配置](config/examples/nabu-tripleboot.conf) 保留 Ubuntu 和注释的 Arch 路径，并按本分支改用独立 EFI。自定义背景和 Linux 文件须自行准备。

## Linux：保留 rEFInd

按 [TheMojoMan/xiaomi-nabu](https://github.com/TheMojoMan/xiaomi-nabu) 准备适配的 Linux、EFI 和 efi.tar，然后执行：

```cmd
py prepare_linux.py --efi-tar "E:\Download\efi.tar" --boot-conf "E:\backup\boot.conf" --kernel "\EFI\ubuntu\6.14.11.efi"
```

复制生成的 EFI/visor/refind 和合并后的 boot.conf 到平板 ESP。Linux 条目继续使用 nabu_action=linux：

```ini
entry {
    name=Ubuntu
    kernel=\EFI\ubuntu\6.14.11.efi
    nabu_action=linux
}
```

其他发行版填写对应 ARM64 EFI / EFI Stub / UKI / GRUB 路径，按需配置 initrd、cmdline 和 uuid。每次选择生成并核对 rEFInd 临时配置，该目录须可写。rEFInd 使用独立子目录，不覆盖主菜单 EFI/BOOT。

本分支由原 BDS 正常加载，不重复发送 EndOfDxe，不将未知固件厂商改为 Project Silicium。Visor 的加密容器、LUKS 辅助注入、sha256 固定约束不能直接交给原版 rEFInd，这类 linux 动作会报错；需要这些上游功能时使用普通条目，不设置 nabu_action。

## 更新与日志

```cmd
py update_visor.py --fd "E:\path\nabu-NoSB.img"
```

追加 --check-only 只检查，或 --boot 更换基础 boot。更新脚本检查并重用移植补丁，失败不发布；GNU-EFI、Renegade 保持固定版本。详见 [更新说明](docs/UPSTREAM_UPDATE.md)。

只更新菜单程序或主题时复制 ESP 文件；Android 内核、Recovery/root、FD 或分流器改变时重新生成并刷入 boot。默认关闭 Visor、Linux 交接、rEFInd 文件日志，保留启动错误提示；旧日志不会自动删除。

## 来源与许可

- [Visor-BootManager](https://github.com/IO-ZetZor/Visor-BootManager)：完整 ARM64 菜单、配置和主题，版本见 port/visor-upstream.json。
- [Renegade edk2-msm](https://github.com/edk2-porting/edk2-msm)：原版标记写入与扫描/清除循环，私有桥接实现 PSCI 复位。
- [rEFInd-nabu](https://github.com/rodriguezst/rEFInd-nabu)：从用户 efi.tar 复用，不参与编译。
- [AOSP bootloader_message](https://android.googlesource.com/platform/bootable/recovery/+/refs/heads/main/bootloader_message/bootloader_message.cpp)：Recovery 请求格式。
- [Issue #1 / fingerboy2019](https://github.com/CLF184/nabu-visor-dualboot/issues/1)：原样 FD、ESP 应用和 PSCI 交接方案。

本项目适配和脚本采用 MIT，上游保留各自许可，见 [NOTICE.md](NOTICE.md)。输入固件、APK 和构建产物不上传源码仓库。
