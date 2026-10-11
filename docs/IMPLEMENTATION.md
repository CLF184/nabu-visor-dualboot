# nabu 移植实现

本项目以 Visor 的完整 ARM64 功能清单为基础。适配集中在平台接口、FD/BDS 交接、Android 标记启动和 Recovery 请求，保留上游配置格式、外部资源与普通 EFI 加载逻辑。

此前交付镜像已验证 Android、Windows、Recovery 按钮和按键入口。最新构建包含浏览路径优先、上游错误恢复及构建发布修复，已通过编译和镜像校验，尚未真机验证。Linux 未在本机验证，具体成品状态见构建输出的 `manifest.json`。

## 菜单与固件

Visor 编译为 AArch64 DXE 驱动 `NabuBootMenu.efi`，插入基础 FD，挂接原 BDS 入口并在其前显示菜单。配置使用上游 `CONFIG_FILE` 常量，统一读取 ESP 的 `\EFI\visor\boot.conf`；图标和背景继续从 `\EFI\visor\` 加载。基础 FD 的其他可执行模块保留原字节。

横屏、音量键切换系统及电源动作、电源键松开确认是 nabu 适配。默认首次 5 秒无输入自动启动 Android，输入后取消本次倒计时。功能启用范围仅遵循上游 ARM64 架构和依赖限制。

## Android 标记配对

Renegade 源码固定为 `e1952621f419f8db60ed28271264e1b5184c571d`。构建直接读取子模块中的 `Reboot2PayloadApp.c` 并校验 Git blob。

写入端将 `0x17FE00000` 的 128 个半字设为 `0x5241`，写回并失效全部 256 字节缓存，然后调用 `EfiResetWarm`。EDK2 到 GNU-EFI 的适配通过运行时表和库函数桥接完成；缓存行大小从 `CTR_EL0` 读取。

接收端沿用上游 `BootShim.Dualboot.S` 的扫描和清除循环，与原厂 Recovery 识别代码一起追加在 FD 后。任意标记匹配时，清空全部 256 字节并跳回原内核；未匹配且不是原厂 Recovery 环境时，接回 Nabu2 的 FD 复制入口。原 Android 内核 `0xb0` 后的主体和解压后的 ramdisk 保留。

标记只表示“下一次绕过菜单进入原内核”。Android 或 Recovery 的实际目标由原厂 bootloader 准备的启动环境决定。菜单驱动与独立 Android EFI 共用 `src/nabu_reset.zig`，通过 `port/renegade/bridge.c` 调用同一套上游程序。

## Recovery

### 菜单按钮

`port/recovery/recovery.c` 按 GPT 分区名找到唯一 `misc` 逻辑分区，更新 AOSP BCB：

| 字段 | 偏移/长度 | 请求值 |
| --- | --- | --- |
| command | 0 / 32 字节 | `boot-recovery` |
| recovery | 64 / 768 字节 | `recovery\n` |

仅这两个字段改变。512 字节块读写开头 2048 字节；4096 字节块读写完整 4096 字节并保留其他内容。写入、刷新与读回核对成功后，调用 Android 的标记桥接函数并热重启，下一次启动绕过菜单。

请求写入失败时不写标记、不重启，并尝试恢复原始 BCB；恢复失败会显示原因。上游标记程序在 `ResetSystem` 异常返回时进入 `CpuDeadLoop`。按钮使用平板已有的 Recovery，不嵌入或替换 Recovery 镜像。

### 原厂 Recovery 入口

当前 nabu 的正常启动 FDT 含 `androidboot.force_normal_boot=1`，按键或 `fastboot reboot recovery` 的 Recovery 环境不含该参数。判断放在内核入口、FD 交接之前，以保留原厂传入的启动寄存器和设备树。

`port/recovery/native_entry.S` 在 Renegade 标记未命中后，只读检查 x0 指向的 FDT。仅当版本、范围、节点与字符串边界有效，根 model 为 `NABU`，`/chosen/bootargs` 含 `androidboot.hardware=qcom` 且不含正常启动参数时，清除标记并跳回原内核。保留 x0～x3、FDT 与 ramdisk，不使用栈、不写 BCB、不重启。正常启动或无效/不匹配的数据仍进入 FD/Visor。

这保留了电源＋音量上的原厂 Recovery 入口；bootloader 为 `fastboot reboot recovery` 或 `adb reboot recovery` 提供相同环境时也直接继续原内核。判断针对当前 nabu 的命令行协议，不解析 bootconfig。相关启动约定见 [AOSP ramdisk 文档](https://source.android.com/docs/core/architecture/partitions/ramdisk-partitions)。

开机不执行 BCB 自动检测或热重启；菜单 Recovery 按钮仍保留写请求、标记和热重启流程。

## Windows 与普通 EFI 条目

Android 配置 `nabu_action=android`，Windows 配置 `nabu_action=bds`。旧配置缺少这些键会进入普通 EFI 加载路径；升级时须检查实际 ESP 的 `boot.conf` 并替换配套 Android EFI，保留个人外观设置。

Windows 的 BDS 动作退出 Visor 并继续原固件启动流程。配置中的 `kernel=...bootmgfw.efi` 用于展示/识别，不是该动作的实际 `LoadImage` 目标。固件默认启动项改变时，BDS 的启动结果也会改变。

普通条目不设置 `nabu_action`，沿用上游 EFI 加载器。浏览器或快照明确覆盖内核/EFI 路径时，本次优先使用上游加载器，不修改保存的动作条目。加载失败进入上游错误恢复界面，文字操作需要 USB 键盘。Linux 的 ARM64 EFI 程序或 EFI Stub 内核可走普通路径，但兼容性需实际验证；三系统应配置独立的 Windows 与 Linux 入口。

## 构建与发布

`port/visor/NABU.patch` 只应用到 Visor 的临时副本。`port/recovery/overlay.py` 额外生成三个覆盖文件：

1. `src/visor_platform.c`：第三个电源按钮调用 Recovery 请求。
2. `third_party/visor/src/gui/gui_panels.c`：显示 Recovery 标签，保留原图标。
3. `third_party/visor/src/core/main.c`：请求失败显示原因并取消倒计时。

构建校验上游源码、固件模块保留、原内核主体与 ramdisk 保留，以及打包后内核匹配。BCB 模拟存储检查覆盖修改范围、512/4096 字节块、对齐、失败恢复和标记调用顺序。

全部成品和 ESP 资源先在临时目录生成并校验，再由 `build_publish.py` 发布。已有目录允许覆盖同名生成文件，无关文件保留。替换前文件备份到输出目录 `.build-backup/backup.zip`；`publication.json` 记录原路径。发布失败尝试恢复已替换文件，构建失败保留原成品。测试覆盖成功覆盖、准备失败及部分发布后的回滚。

三个依赖用子模块固定版本，工作树保持干净。首次构建自动生成功能头文件与清单，缺少的默认资源从 Visor 子模块准备到 ESP 副本，并生成 Android EFI。原始固件、工具安装和输出镜像不提交 Git。
