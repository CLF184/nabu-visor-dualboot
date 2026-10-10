# nabu Visor 启动菜单

小米平板 5（nabu）的 Visor 图形启动菜单。横屏显示，音量键选择，电源键确认；进入后 5 秒无操作自动启动 Android，任何输入都会取消本次倒计时并等待操作。

**当前实现已经确认能进入 Android、Windows 和 Recovery。** 用户于 2026-10-10 确认 Recovery 按钮可直接进入 Recovery。Linux 启动还未在本机验证。

工程只保留当前实现。构建成品写入本机 `dist/`，文件名不带版本号。GitHub 只保存源码、补丁、配置和三个编译依赖的子模块指针；镜像、原始固件、Windows 工具和 Zig 安装目录不提交。

## 获取源码与准备依赖

```powershell
git clone https://github.com/CLF184/nabu-visor-dualboot.git
cd nabu-visor-dualboot
git submodule update --init --depth 1
py -m pip install --target tools/python ziglang==0.14.1
```

只初始化主工程的三个子模块，不需要 `--recursive`。Renegade 的嵌套 EDK2 和工具链不参与本程序编译。

| 子模块 | 编译用途 | 固定提交 |
| --- | --- | --- |
| `third_party/visor` | 完整 ARM64 Visor 菜单 | `71a05b5945e37903cfeeaf69c76220fdf5861ac9` |
| `third_party/gnu-efi` | EFI C 库和头文件 | `87162316d1d04a12d5cc54f77bb7df117b73b146` |
| `third_party/renegade` | Reboot2PayloadApp 与 BootShim 标记循环 | `e1952621f419f8db60ed28271264e1b5184c571d` |

Visor 的当前提交也记录在 `port/visor-upstream.json`。nabu 补丁位于 `port/visor/NABU.patch`，只在临时构建副本中应用，子模块保持干净。更新脚本只更新 Visor；GNU-EFI 与 Renegade 保持固定版本。

准备可运行的 Windows 版 `magiskboot.exe` 和 `DualBootKernelPatcher-x86_64.exe`，放到 `tools/official/`。本仓库包含 nabu 补丁配置与 176 字节入口代码，两个工具的可执行文件需自行提供。DualBootKernelPatcher 上游见 [Project-Aloha/DualBootKernelPatcher](https://github.com/Project-Aloha/DualBootKernelPatcher)。

还需要自己的**原始 Android boot.img** 和**原始 nabu UEFI .fd**。用 `--boot`、`--fd` 指定路径，或放到默认的 `tools/official/stock-boot.img`、`tools/official/nabu-uefi.fd`。这些文件保留在本机，不上传 GitHub。

## 成品文件

| 文件 | 用途 |
| --- | --- |
| `nabu-visor-boot.img` | 128 MiB 双启动 boot 镜像，含 FD、菜单和内核入口分流 |
| `nabu-visor-esp.zip` | 配置、图标、背景和配套 Android EFI 程序 |
| `nabu-visor.fd` | 与 boot 镜像配套的 3 MiB 固件 FD |
| `manifest.json` | 输入输出来源、哈希、检查与真机结果 |
| `SHA256SUMS.txt` | 文件 SHA-256 校验值 |

已有可用的 Visor ESP 配置时，只更新 boot 镜像即可。菜单程序位于 FD 中，外观配置与素材位于 ESP。

## 菜单操作与启动流程

- 音量上/下：切换系统条目与左下角电源动作。
- 电源键：松开时确认当前选项。
- 首次 5 秒无操作：自动选 Android；有操作后本次不再自动启动。
- 左下角三个按钮：关机、重启、**Recovery**。Recovery 沿用原版固件设置图标。
- 保留上游键盘、编辑器、文件浏览等功能。Esc 打开 Visor 故障恢复控制台，它与 Android Recovery 是两个不同入口。

| 选择 | 实际操作 |
| --- | --- |
| Android | 调用 Renegade 原版标记程序，写入 128 个 `0x5241` 标记后热重启；内核入口命中后清除标记并回到原内核 |
| Windows | 退出菜单，交回 FD 原始 BDS 启动流程 |
| Recovery | 先向 `misc` 写入 `boot-recovery` 请求并验证，再调用同一套标记/热重启程序，重启后绕过菜单进入已有 Recovery |

Recovery 按钮使用平板已经安装的 Recovery，不嵌入或替换 Recovery 镜像。标记位于 `0x17FE00000`，共 256 字节。具体适配说明见 [docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md)。

## 安装与回滚

在电脑的 PowerShell 中进入项目目录，使用已安装的 platform-tools，沿用本机此前成功的 boot 分区刷入方式：

```powershell
fastboot devices
fastboot flash boot .\dist\nabu-visor-boot.img
fastboot reboot
```

ESP 包解压后的内容从 `EFI/visor/` 开始。把这个目录复制到**平板 EFI 分区**的 `\EFI\visor\`。通过 Android 文件传输看到平板 ESP 时，可直接复制；在平板的 Windows 中，也可先挂载 EFI 分区再复制。

`install/install-windows.cmd` 供平板的 Windows 环境以管理员运行，复制工程 `esp/` 中的配置和素材；它挂载运行设备自己的 ESP。

回滚时，刷回你此前备份且确认适用的 boot 镜像。仅删除 ESP 的 `\EFI\visor\` 不能移除 boot 中的菜单。`install/restore-windows.cmd` 用于移除平板 Windows 中的 ESP 配置。

## 编译命令：直接复制使用

先在 PowerShell 中切换到工程目录：

```powershell
cd "E:\你的文件夹\nabu-visor-dualboot"
```

### 1. 使用工程自带的原始 boot 和 FD

```powershell
py build.py
```

默认输入和输出如下：

| 项目 | 路径 |
| --- | --- |
| 原始 Android boot | `tools/official/stock-boot.img` |
| 原始 nabu UEFI FD | `tools/official/nabu-uefi.fd` |
| 输出文件夹 | `dist/` |
| 生成的刷机镜像 | `dist/nabu-visor-boot.img` |
| 生成的配置包 | `dist/nabu-visor-esp.zip` |

**输出目录可以已经存在。同名生成文件直接覆盖，其他文件保留。** 构建临时文件放在独立临时目录，不复用旧解包内容。新生成镜像的 manifest 会记录为尚未真机验证；它不自动继承已有成品的实测结果。

### 2. 指定自己的原始 boot 和 FD

复制下面的**一行命令**，将两个引号中的输入路径改成你的真实文件路径：

```powershell
py build.py --boot "E:\你的文件夹\boot.img" --fd "E:\你的文件夹\nabu.fd" --out-dir ".\dist"
```

| 参数 | 含义 |
| --- | --- |
| `--boot` | 要保留的原始 Android boot 镜像；使用未加本菜单补丁的输入 |
| `--fd` | 要加入菜单的 nabu UEFI `.fd`；使用未插入本菜单的原始 FD |
| `--out-dir` | 输出文件夹；`".\dist"` 表示当前项目中的 dist，已有同名产物会覆盖 |

不填写某个参数时，使用默认值。查看所有参数：

```powershell
py build.py --help
```

### 3. `py -3 -X utf8` 是什么

- `py`：Windows 的 Python 启动器。
- `-3`：明确选择 Python 3。
- `-X utf8`：启用 Python 的 UTF-8 模式，减少中文路径和输出乱码。`-X` 与 `utf8` 是两个参数。
- `build.py`：要运行的构建脚本。

本工程命令可以写成 `py build.py`；当前脚本为子构建程序启用 UTF-8。如果电脑同时安装多个 Python 版本，也可以用 `py -3 build.py` 指定 Python 3。

### 构建依赖

需要 Python 3、Zig **0.14.1**、`tools/official/` 中的 magiskboot 与 DualBootKernelPatcher。本机已准备的工具可继续使用；从 GitHub 克隆后按前文准备工具与原始输入。安装 Zig 时执行：

```powershell
py -m pip install --target tools/python ziglang==0.14.1
```

构建沿用完整 Visor 功能清单，只按上游 `aarch64` 架构限制排除不支持的功能；保留原版外部配置与资源加载方式。`src/` 是基础适配源码，当前 Android backend 来自 `port/renegade/`，它调用 Renegade 子模块中的原版程序；Visor 补丁和 Recovery 修改只应用到临时构建副本。

## Visor 上游更新

双击工程根目录的 **`update_visor.cmd`**，或运行：

```powershell
py update_visor.py
```

默认获取最新上游、应用完整 nabu 补丁、重新编译并更新同名成品；当前已是该 commit 时无需更新。需要 Python 3、Git 和网络。更新前自动备份，保留个人配置与素材，补丁冲突或编译失败时停止。脚本不刷机。

只检查兼容性而不替换文件：`py update_visor.py --check-only`。指定上游版本：`py update_visor.py --ref <tag或commit>`。完整参数、备份和失败处理见 [docs/UPSTREAM_UPDATE.md](docs/UPSTREAM_UPDATE.md)。

## 修改背景、标题与配置

运行时统一读取平板 ESP 的 **`\EFI\visor\boot.conf`**，沿用 Visor 上游的配置路径。修改该文件后下次进入菜单生效，无需重刷 boot。

把图片 `my-background.png` 放到 `\EFI\visor\backgrounds\`，再修改配置：

```ini
title=Nabu Boot Menu
background=\EFI\visor\backgrounds\my-background.png
timeout=5
default=0
```

`title=none` 隐藏标题。`default` 按条目顺序从 0 开始，目前 0 是 Android。完整键和格式参考包内 `boot.conf.example`。

工程配置源是 `config/boot.conf`。需要把工程的修改打进配置包时，先同步再构建：

```powershell
Copy-Item .\config\boot.conf .\esp\EFI\visor\boot.conf
py build.py
```

保留 Android 的 `nabu_action=android` 和 Windows 的 `nabu_action=bds`。这些键决定实际启动动作；当前 Windows 条目的 `kernel=...bootmgfw.efi` 用于展示/识别，实际控制权交回 BDS。

## UEFI 更新与三系统

Windows 更新仅替换 ESP 上的 Windows 引导文件时，不需要重新打包本镜像。若要更换 boot 中的 UEFI，取得相应原始 nabu `.fd` 后，使用上面的 `--fd` 命令重新打包，再刷入生成的 boot。

脚本接受 `.fd`；厂商 UEFI `.img` 或更新包需要先提取 FD。新的输入须符合当前 nabu/SM8150 工具链的布局，新的 FD 或 Android boot 仍需真机确认。

菜单不限于两个系统，普通条目保留 Visor 的 EFI 加载流程，可以配置 ARM64 EFI 引导程序或带 EFI Stub 的 Linux 内核。但 Linux 还未在本机验证，需要核对镜像、设备树/ACPI、启动参数和固件兼容性。

当前 Windows 的 `nabu_action=bds` 是“继续固件正常启动”，不是固定直接加载 Windows。Linux 安装若改变固件默认启动项，这个条目也可能进入 Linux。三系统需要区分两个入口，不能把 Windows 和 Linux 都设为同一个 `bds` 动作。

## 目录

```text
nabu-visor-dualboot/
├─ README.md                使用、编译、配置说明
├─ build.py                 主构建入口
├─ image_builder.py         镜像与 ESP 包生成程序
├─ update_visor.cmd/.py     一键更新上游、构建与备份
├─ fd_patch.py              固件卷修改与校验
├─ patch_boot.py            官方内核/FD 补丁验证函数
├─ visor_build.py           Visor 功能清单与编译参数
├─ dist/                    当前成品与校验、实测记录
├─ docs/                    当前实现说明
├─ src/                     Nabu 基础适配层
├─ port/renegade/            原版标记程序、接收循环与桥接
├─ port/recovery/            Recovery 请求程序与构建覆盖器
├─ config/                  配置源
├─ esp/EFI/visor/            当前外部配置与资源
├─ install/                 平板 Windows 的 ESP 安装/移除脚本
├─ third_party/             Visor、GNU-EFI、Renegade 子模块及其许可证
├─ tools/                   编译器、原始输入与补丁工具
└─ build/                   自动生成的功能头文件与清单，不提交 Git
```

## 来源与许可

- [Visor-BootManager](https://github.com/IO-ZetZor/Visor-BootManager)，初始 commit `71a05b5945e37903cfeeaf69c76220fdf5861ac9`，BSD-2-Clause；更新后当前版本见 `port/visor-upstream.json`。
- [Renegade edk2-msm](https://github.com/edk2-porting/edk2-msm/tree/e1952621f419f8db60ed28271264e1b5184c571d)，原版文件与许可证来自 `third_party/renegade/` 子模块。
- [AOSP bootloader_message](https://android.googlesource.com/platform/bootable/recovery/+/refs/heads/main/bootloader_message/bootloader_message.cpp)，Recovery 请求格式与字段更新方式。
- Nabu 适配与 FD 修改遵循各文件的 MIT 标识；GNU-EFI 与补丁工具遵循其上游许可。
