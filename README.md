# nabu Visor 启动菜单

小米平板 5（nabu）的 Visor 图形启动菜单。横屏显示，音量键选择，电源键确认。进入后 5 秒无操作自动启动 Android；有操作则取消本次倒计时，等待确认。

沿用 Visor 的外部配置、主题资源和完整 ARM64 功能清单。菜单嵌入 UEFI FD，配置与素材放在平板 ESP 的 `\EFI\visor\`。Android 使用 Renegade 标记配对启动，Windows 继续原固件 BDS 流程，Recovery 启动平板已有的 Recovery。

此前交付镜像已验证 Android、Windows、Recovery 按钮与电源＋音量上入口。**最新构建已通过编译和镜像校验，尚未真机验证。** 每次构建的结果以 `dist/manifest.json` 为准；Linux 尚未在本机验证。

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
| `third_party/renegade` | Reboot2PayloadApp 与 BootShim 标记循环 |

准备 Windows 版 `magiskboot.exe` 和 `DualBootKernelPatcher-x86_64.exe`，放到 `tools/official/`。补丁工具上游：[Project-Aloha/DualBootKernelPatcher](https://github.com/Project-Aloha/DualBootKernelPatcher)。仓库包含 nabu 补丁配置与入口代码，工具可执行文件需自行提供。

另外准备**未加本菜单补丁的 Android boot.img** 和**原始 nabu UEFI .fd**。默认路径如下，也可在命令中指定：

| 项目 | 默认路径 |
| --- | --- |
| 原始 Android boot | `tools/official/stock-boot.img` |
| 原始 nabu UEFI FD | `tools/official/nabu-uefi.fd` |
| 输出目录 | `dist/` |

GitHub 保存源码、补丁、配置和子模块指针；原始固件、工具安装目录和编译产物仅保留在本机。

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
| `RenegadeMarkerLoops.bin` | 配对标记循环与原厂 Recovery 入口代码 |
| `manifest.json` / `SHA256SUMS.txt` | 构建来源、验证状态与文件校验值 |

## 安装与回滚

先备份平板现有 boot 镜像和 ESP 的 `\EFI\visor\` 配置。解压 `nabu-visor-esp.zip`，把其中的 `EFI/visor/` 复制到**平板 EFI 分区**的 `\EFI\visor\`，然后沿用本机已成功的 boot 分区刷入方式：

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
- 左下角依次为关机、重启、Recovery。Recovery 沿用 Visor 原版固件设置图标。
- 电源＋音量上触发原厂 Recovery 环境时，入口直接继续原内核；bootloader 为 `fastboot reboot recovery` / `adb reboot recovery` 提供相同环境时也走此入口。
- 保留上游文件浏览、编辑器和键盘功能。浏览器或快照覆盖 EFI/内核路径时，优先启动本次选中的文件。普通 EFI 加载失败沿用上游错误恢复界面，文字操作需要 USB 键盘。
- Esc 打开的 Visor 恢复控制台与 Android Recovery 是两个不同入口。

| 选择 | 实际操作 |
| --- | --- |
| Android | 写入 128 个 `0x5241` 标记后热重启；内核入口清除标记并继续原内核 |
| Windows | 退出菜单，交回 FD 原始 BDS 启动流程 |
| Recovery | 向 `misc` 写入并核对 `boot-recovery` 请求，再使用同一套标记与热重启流程 |

Recovery 按钮使用已有 Recovery 镜像。原厂 Recovery 入口识别针对当前 nabu 的 FDT 启动参数约定；完整适配和限制见 [实现说明](docs/IMPLEMENTATION.md)。

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

Windows 更新只替换 ESP 引导文件时，无需重新构建。要更新 boot 中的 UEFI，取得原始 nabu `.fd` 后，用 `--fd` 重新构建并刷入生成的 boot。UEFI `.img` 或更新包需先提取 FD；新输入须符合 nabu/SM8150 工具链布局，并重新确认真机启动。

菜单可以添加其他系统。普通条目不设置 `nabu_action`，沿用 Visor 的 EFI 加载流程，可配置 ARM64 EFI 引导程序或 EFI Stub 内核；Linux 的设备树/ACPI、参数和固件兼容性仍需实际验证。

Windows 的 `nabu_action=bds` 跟随固件默认启动项。安装 Linux 若改变默认项，此条目也可能进入 Linux；三系统需配置不同的引导入口。

## 目录

```text
nabu-visor-dualboot/
├─ build.py                 构建入口
├─ image_builder.py         镜像与 ESP 包生成
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
├─ port/recovery/            Recovery 请求、入口识别与构建覆盖器
├─ third_party/             三个编译依赖子模块
├─ tests/                   成品发布与回滚测试
├─ tools/                   本地编译器、补丁工具和原始输入
├─ build/                   自动生成的功能头文件与清单
└─ dist/                    最新成品、校验值与最近一次备份
```

## 来源与许可

- [Visor-BootManager](https://github.com/IO-ZetZor/Visor-BootManager)，BSD-2-Clause；当前版本与补丁哈希见 `port/visor-upstream.json`。
- [Renegade edk2-msm](https://github.com/edk2-porting/edk2-msm)，标记程序及 BootShim 循环来自固定版本子模块。
- [AOSP bootloader_message](https://android.googlesource.com/platform/bootable/recovery/+/refs/heads/main/bootloader_message/bootloader_message.cpp)，Recovery 请求格式。

本项目适配和构建脚本采用 MIT 许可；上游代码及资源遵循各自许可，详见 [NOTICE.md](NOTICE.md)。
