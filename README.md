# nabu Visor 启动菜单

小米平板 5（nabu）的 Visor 图形启动菜单。横屏显示，音量键选择，电源键确认。进入后 5 秒无操作自动启动 Android；有操作则取消本次倒计时，等待确认。

沿用 Visor 的外部配置、主题资源和完整 ARM64 功能清单。菜单嵌入 UEFI FD，配置与素材放在平板 ESP 的 `\EFI\visor\`。Android 使用 Renegade 标记配对启动，Windows 继续原固件 BDS 流程，Recovery 启动平板已有的 Recovery。

支持在同一菜单中选择 Android、Windows 和 Linux，也可通过菜单按钮或电源＋音量上进入 Recovery。

## 获取源码与准备依赖

在 Windows PowerShell 中执行：

```powershell
git clone https://github.com/CLF184/nabu-visor-dualboot.git
cd nabu-visor-dualboot
git submodule update --init --depth 1
py -m pip install --target tools/python ziglang==0.14.1
```

需要 Python 3、Git 和 Zig **0.14.1**。只初始化三个直接参与编译的子模块，无需 `--recursive`：

| 子模块 | 用途 |
| --- | --- |
| `third_party/visor` | Visor 菜单、配置解析与主题资源 |
| `third_party/gnu-efi` | EFI C 库和头文件 |
| `third_party/renegade` | Reboot2PayloadApp、BootShim 标记循环与 BootSlotDxe |

仓库已包含构建使用的 Windows x64 版 `magiskboot.exe` 和 `DualBootKernelPatcher-x86_64.exe`，位于 `tools/official/`，拉取后无需另行下载补丁工具。沿用现用工具版本，不改变打补丁流程；来源、许可证和 SHA-256 见 [补丁工具说明](tools/official/README.md)。

另外准备**未加本菜单补丁的 Android boot.img** 和**原始 nabu UEFI .fd**。默认路径如下，也可在命令中指定：

| 项目 | 默认路径 |
| --- | --- |
| 原始 Android boot | `tools/official/stock-boot.img` |
| 原始 nabu UEFI FD | `tools/official/nabu-uefi.fd` |
| 输出目录 | `dist/` |

GitHub 保存源码、补丁、配置、上述两个补丁工具及子模块指针；原始固件、Python/Zig 安装目录和编译产物仅保留在本机。

## 编译

在项目目录运行，使用默认输入：

```powershell
py build.py
```

指定自己的输入文件与输出目录：

```powershell
py build.py --boot "E:\path\original-android-boot.img" --fd "E:\path\new-nabu.fd" --out-dir ".\dist"
```

- `--boot`：原始 Android boot 镜像路径。
- `--fd`：未插入本菜单的 nabu UEFI FD 路径。
- `--out-dir`：输出目录；省略则使用 `dist/`。

**输出目录可以已存在。同名生成文件在全部校验通过后覆盖，其他文件保留。** 构建先在独立临时目录完成；发布失败尝试回滚。替换前的文件保存在输出目录 `.build-backup/backup.zip`，只保留最近一次。压缩包内 `publication.json` 记录各备份文件对应的原路径。

查看所有参数：`py build.py --help`。

### 成品

| 文件 | 用途 |
| --- | --- |
| `nabu-visor-boot.img` | 可刷入的双启动 boot 镜像 |
| `nabu-visor-esp.zip` | 配置、图标、背景和配套 Android EFI 程序 |
| `nabu-visor.fd` | 已加入菜单的固件 FD |
| `NabuBootMenu.efi` / `NabuAndroid.efi` | 菜单驱动与独立 Android EFI 程序 |
| `BootSlotDxe.efi` | 原版 Renegade A/B 槽位维护驱动，已包含在 FD 中 |
| `RenegadeMarkerLoops.bin` | 配对标记循环与原厂 Recovery 入口代码 |
| `manifest.json` / `SHA256SUMS.txt` | 构建来源与文件校验值 |

## 安装与回滚

先备份平板现有 boot 镜像和 ESP 的 `\EFI\visor\` 配置。解压 `nabu-visor-esp.zip`，把其中的 `EFI/visor/` 复制到**平板 EFI 分区**的 `\EFI\visor\`，然后刷入 boot 分区：

```powershell
fastboot devices
fastboot flash boot .\dist\nabu-visor-boot.img
fastboot reboot
```

更新旧版时，在实际使用的 `boot.conf` 中保留 Android 的 `nabu_action=android`、Windows 的 `nabu_action=bds`，并更新配套的 `NabuAndroid.efi`。自己的标题、背景设置可合并保留。

在平板 Windows 上，可用管理员权限运行 `install/install-windows.cmd`，把工程 `esp/` 内的配置和素材复制到运行设备自己的 ESP。它会覆盖同名文件，先备份个人配置；在电脑上构建时不运行此脚本。

回滚时刷回此前备份的 boot 镜像。`install/restore-windows.cmd` 只移除 ESP 的菜单配置；单独删除配置不能移除 boot 内的菜单。

## 操作与启动方式

- 音量上/下切换系统条目和左下角电源动作，电源键松开时确认。
- 默认 Android，首次 5 秒无输入自动启动；输入后本次不再倒计时。
- bootloader 指定关机充电模式时，直接继续原 Android 充电路径，不进入菜单或倒计时；基础 boot 的内核与 ramdisk 需支持关机充电。
- 左下角依次为关机、重启、Recovery。Recovery 沿用 Visor 原版固件设置图标。
- 电源＋音量上触发原厂 Recovery 环境时，入口直接继续原内核；bootloader 为 `fastboot reboot recovery` / `adb reboot recovery` 提供相同环境时也走此入口。
- 保留上游文件浏览、编辑器和键盘功能。浏览器或快照覆盖 EFI/内核路径时，优先启动本次选中的文件。普通 EFI 加载失败沿用上游错误恢复界面，文字操作需要 USB 键盘。
- Esc 打开的 Visor 恢复控制台与 Android Recovery 是两个不同入口。

| 选择 | 实际操作 |
| --- | --- |
| Android | 写入 128 个 `0x5241` 标记后热重启；内核入口清除标记并继续原内核 |
| Windows | 退出菜单，交回 FD 原始 BDS 启动流程 |
| Linux | 原版 rEFInd 加载该条目的 EFI/内核、initrd 和参数 |
| Recovery | 向 `misc` 写入并核对 `boot-recovery` 请求，再使用同一套标记与热重启流程 |

Recovery 按钮使用已有 Recovery 镜像。原厂 Recovery 入口识别针对当前 nabu 的 FDT 启动参数约定；完整适配和限制见 [实现说明](docs/IMPLEMENTATION.md)。

FD 同时包含原版 Renegade `BootSlotDxe`。每次进入 UEFI 时，它刷新当前 A/B 槽位的重试次数并标记启动成功，避免进入菜单或其他系统时因未被 Android 标记成功而耗尽启动重试次数。按上游逻辑也会清除两个槽位的不可启动标志，并补满尚未成功槽位的重试次数。若输入 FD 已含此驱动，保留原模块，不重复插入；不支持 A/B 的分区布局会跳过处理。

## 修改配置、背景和标题

运行时统一读取平板 ESP 的 **`\EFI\visor\boot.conf`**。修改后下次进入菜单生效，无需重刷 boot。

把 `my-background.png` 放到 `\EFI\visor\backgrounds\`，再修改配置：

```ini
title=Nabu Boot Menu
background=\EFI\visor\backgrounds\my-background.png
timeout=5
default=0
```

`title=none` 隐藏标题；`default` 按条目顺序从 0 开始，目前 0 为 Android。完整配置格式见 `boot.conf.example`。

电脑上的配置源为 `config/boot.conf`，配置包使用 `esp/EFI/visor/boot.conf`。要把配置源的修改打包，先同步：

```powershell
Copy-Item .\config\boot.conf .\esp\EFI\visor\boot.conf
py build.py
```

保留 Android 和 Windows 的 `nabu_action`。当前 Windows 条目的 `kernel=...bootmgfw.efi` 用于展示/识别，实际启动交回 BDS。

## 更新 Visor、UEFI 与添加系统

双击 `update_visor.cmd`，或执行 `py update_visor.py`。脚本获取最新 Visor、应用 nabu 补丁、重新构建并备份替换前文件，保留工程中的个人配置与素材。GNU-EFI 和 Renegade 保持固定版本。

只检查兼容性：`py update_visor.py --check-only`。脚本不刷机，补丁冲突或编译失败时停止；参数和恢复方式见 [上游更新说明](docs/UPSTREAM_UPDATE.md)。

Windows 更新只替换 ESP 引导文件时，无需重新构建。要更新 boot 中的 UEFI，取得原始 nabu `.fd` 后，用 `--fd` 重新构建并刷入生成的 boot。UEFI `.img` 或更新包需先提取 FD；新输入须符合 nabu/SM8150 工具链布局。

菜单可以添加其他系统。普通条目不设置 `nabu_action`，沿用 Visor 的 EFI 加载流程，可配置 ARM64 EFI 引导程序或 EFI Stub 内核；Linux 需使用适配 nabu 的内核、设备树/ACPI 和启动参数。

三系统配置保留 Android 的 `nabu_action=android`、Windows 的 `nabu_action=bds`，并为每个 Linux 条目设置 `nabu_action=linux`。Windows 的 BDS 动作跟随固件默认启动项，因此安装 Linux 时应保留 Windows 为该默认目标；Linux 则通过下述独立 rEFInd 目录加载。

## Linux：复用 Nabu rEFInd

Linux 条目设置 `nabu_action=linux`，走 `Visor → 原版 Nabu rEFInd → 所选 Linux 引导文件`。可以配置多个发行版，或把目标设为 ARM64 GRUB/shim；发行版内核仍需适配 nabu。

先按 [TheMojoMan/xiaomi-nabu](https://github.com/TheMojoMan/xiaomi-nabu) 准备 Linux 根分区和 EFI 文件，再准备项目配套 `efi.tar`。在本项目目录运行：

```powershell
py prepare_linux.py --efi-tar "E:\Download\efi.tar" --boot-conf "E:\backup\boot.conf" --kernel "\EFI\ubuntu\6.14.11.efi"
```

`--boot-conf` 指定平板实际配置的备份，保留个人背景及其他条目；省略时使用 `esp/EFI/visor/boot.conf`。`--entry-name` 默认 Ubuntu，指定其他名称可添加或更新其他 Linux 条目。输出在 `dist/linux-esp/`，也可用 `--out-dir` 指定；同名生成文件备份后覆盖。

将输出的 `EFI/visor/refind/` 和 `EFI/visor/boot.conf` 复制到平板 ESP，并安装支持 `linux` 动作的新 boot 镜像。此动作需要更新一次 boot；之后修改发行版路径和参数无需重新编译或刷入。rEFInd 放在独立目录，保留原版程序、驱动、主题和 `refind.conf`，不覆盖 `EFI/BOOT`。

```ini
entry {
    name=Ubuntu
    icon=\EFI\visor\icons\ubuntu.png
    kernel=\EFI\ubuntu\6.14.11.efi
    nabu_action=linux
}

entry {
    name=Other Linux
    icon=\EFI\visor\icons\linux.png
    kernel=\EFI\linux\vmlinuz.efi
    initrd=\EFI\linux\initrd.img
    cmdline=root=PARTLABEL=linux rw
    nabu_action=linux
}
```

有内置参数/ramdisk 的 UKI 通常不用额外设置 `cmdline`/`initrd`。其他分区可指定 Visor 原有的 `uuid`（GPT 分区 GUID）。每次选择后，适配层生成并回读核对 `EFI/visor/refind/nabu-selected.conf`，合并原 refind.conf 内容以保留一级主题 include，使用 rEFInd 原生 `volume/loader/initrd/options` 和立即自动启动；因此该目录需可写。更换发行版只改 `boot.conf` 中对应条目的路径和参数，不手改生成文件。

这一动作适用于 rEFInd 可加载的普通 ARM64 EFI/EFI Stub/UKI 文件。Visor 的加密镜像容器、LUKS 辅助注入和 `sha256` 固定值无法直接转交给未修改的 rEFInd；带这些设置的 `linux` 动作会报错，不会忽略约束。需要这些 Visor 功能时保留普通条目（不设置动作），原有加载功能仍完整保留。文件浏览器或快照临时覆盖目标时也继续沿用原 Visor 加载器。

Linux 交接补齐两个原 BDS 通常提供的条件：发送 EndOfDxe 通知；仅在实际 EFI 系统表缺少厂商字段时填入当前基础 FD 的原值并重算校验。这样保留 rEFInd 原版加载器、所有驱动和主题。若 rEFInd 返回，恢复所补字段。厂商值对应当前基础 FD 的 `Project Silicium`；更换其他来源的 FD 时须核对，详见 [实现说明](docs/IMPLEMENTATION.md)。

Android、Windows、Recovery 的动作保持原样，Windows 继续原固件 BDS/Secure Boot 流程。Linux 接入不修改 Secure Boot 开关、证书或固件认证模块；若固件拒绝某 EFI，仍会报错，rEFInd 本身不是签名绕过工具。

要把 rEFInd 资源包含在完整镜像配置包中，可生成到 `--out-dir .\esp` 后执行 `py build.py`。rEFInd 从输入的 `efi.tar` 复用，不参与菜单编译，不新增编译子模块。

## 日志

默认构建关闭三类文件日志：

- Visor 文件日志：配置模板为 `file_log=0`，镜像也关闭文件记录，旧 ESP 配置不能重新开启。
- Linux 交接日志：不再写入 `EFI/visor/refind/nabu-handoff.log`。
- rEFInd 日志：每次选择 Linux 生成的主配置设置 `log_level 0`，覆盖基础 `refind.conf` 的日志级别。

关闭日志保留 Linux 启动修复和原有启动错误提示。旧日志不会自动删除；生成镜像不会修改平板上的配置或文件。

## 目录

```text
nabu-visor-dualboot/
├─ prepare_linux.py         rEFInd Linux 配置与 ESP 包生成
├─ build.py                 构建入口
├─ image_builder.py         镜像与 ESP 包生成
├─ boot_slot_build.py       编译原版 BootSlotDxe 与 BootSlotLib
├─ build_publish.py         成品发布、备份与失败回滚
├─ fd_patch.py              固件卷修改与校验
├─ patch_boot.py            内核/FD 补丁验证
├─ visor_build.py           上游功能清单与编译参数
├─ update_visor.cmd/.py     上游更新入口与脚本
├─ config/                  默认配置源
├─ docs/                    实现与更新说明
├─ esp/EFI/visor/            打包用外部配置与资源
├─ install/                 平板 Windows 的 ESP 安装/移除脚本
├─ src/                     nabu 平台适配层
├─ port/visor/              Visor 补丁
├─ port/renegade/            标记程序的 ABI 桥接与兼容头文件
├─ port/boot-slot/           独立 BootSlotDxe 的 ABI 桥接、来源与许可
├─ port/recovery/            Recovery 请求、入口识别与构建覆盖器
├─ port/refind/              外部 rEFInd 资源的许可说明
├─ third_party/             三个编译依赖子模块
├─ tests/                   发布回滚、横屏绘制及 Linux 交接主机测试
├─ tools/                   本地编译器、补丁工具和原始输入
├─ build/                   自动生成的功能头文件与清单
└─ dist/                    最新成品、校验值与最近一次备份
```

## 来源与许可

- [Visor-BootManager](https://github.com/IO-ZetZor/Visor-BootManager)，BSD-2-Clause；当前版本与补丁哈希见 `port/visor-upstream.json`。
- [Renegade edk2-msm](https://github.com/edk2-porting/edk2-msm)，标记程序及 BootShim 循环来自固定版本子模块。
- [rEFInd-nabu](https://github.com/rodriguezst/rEFInd-nabu)，Linux 中间加载器；外部文件从用户提供的 `efi.tar` 提取，不加入编译子模块。
- [AOSP bootloader_message](https://android.googlesource.com/platform/bootable/recovery/+/refs/heads/main/bootloader_message/bootloader_message.cpp)，Recovery 请求格式。

本项目适配和构建脚本采用 MIT 许可；上游代码及资源遵循各自许可，详见 [NOTICE.md](NOTICE.md)。
