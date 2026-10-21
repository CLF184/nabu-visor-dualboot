# Windows 补丁工具

本目录包含项目一直使用的两个 Windows x64 可执行文件，未修改或升级。构建脚本直接调用它们，不需要将本目录加入 PATH。

| 文件 | 用途 | 许可证 |
| --- | --- | --- |
| `magiskboot.exe` | 解包、重打包 Android boot 镜像 | GPLv3，见 `licenses/magiskboot-GPL-3.0.txt` |
| `DualBootKernelPatcher-x86_64.exe` | 按 nabu 配置将 FD 与入口代码合入内核 | MIT，见 `licenses/DualBootKernelPatcher-MIT.txt` |

拉取仓库并按根目录 README 准备 Python、Git、子模块和 Zig 后，提供未加菜单补丁的 Android boot.img 与原始 nabu FD，再执行：

```powershell
py build.py --boot "E:\path\original-android-boot.img" --fd "E:\path\new-nabu.fd"
```

工具仅处理输入文件，不会自动连接、刷写平板。原始固件不随仓库提供。

## 来源与对应源码

- DualBootKernelPatcher：[Project-Aloha/DualBootKernelPatcher](https://github.com/Project-Aloha/DualBootKernelPatcher)。本文件沿用原 nabu_DUALBOOT 工程所带版本，其附带的 DuoWoa Authors MIT 声明一并保留；原文件未提供精确构建提交，不声称它是上游最新版。
- magiskboot：[PinNaCode/magiskboot_build](https://github.com/PinNaCode/magiskboot_build)（原 ookiineko/magiskboot_build），`last-ci` 发布的 [Windows x64 UCRT standalone 包](https://github.com/PinNaCode/magiskboot_build/releases/download/last-ci/magiskboot-e159716-release-windows-mingw-w64-ucrt-x86_64-standalone.zip)。已核对上游包内的 exe 与本目录文件 SHA-256 完全相同。
- magiskboot 的[上游对应源码包](https://github.com/PinNaCode/magiskboot_build/releases/download/last-ci/magiskboot_b22b6a4_27002-src.tar.xz) 原样保存在 `sources/magiskboot_b22b6a4_27002-src.tar.xz`。解压后包含 README 构建说明、CMake 配置、补丁、Magisk 与依赖源码及其许可声明；不需要解压即可使用现成工具。

`magiskboot.exe` 只是 boot 镜像处理工具；随仓库提供它不代表生成的镜像会自动获得 Magisk root。

## SHA-256

```text
bb72543569ea6ec2eb4cc55b999be96c5f624a15c8e40afbf1c6bc8d67e4063e  magiskboot.exe
5bd1b6b189e08b648e0766f73090fab4bfccb8bcf7d3239e103cfabf926f030e  DualBootKernelPatcher-x86_64.exe
88f12605f3f892f6ff44f6e178536884a6029a025e4a0a29dd916c7fe607c2da  sources/magiskboot_b22b6a4_27002-src.tar.xz
```

在 PowerShell 中可执行 `Get-FileHash .\tools\official\*.exe -Algorithm SHA256` 检查工具。更新工具时需同时更新对应源码、许可证和这里的来源与校验值。
