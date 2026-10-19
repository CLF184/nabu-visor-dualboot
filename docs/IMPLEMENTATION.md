# nabu 移植实现

本项目以 Visor 的完整 ARM64 功能清单为基础。适配集中在平台接口、FD/BDS 交接、Android 标记启动和 Recovery 请求，保留上游配置格式、外部资源与普通 EFI 加载逻辑。

此前交付镜像已验证 Android、Windows、Recovery 按钮和按键入口。最新构建包含浏览路径优先、上游错误恢复及构建发布修复，已通过编译和镜像校验，尚未真机验证。Linux 未在本机验证，具体成品状态见构建输出的 `manifest.json`。

## 菜单与固件

Visor 编译为 AArch64 DXE 驱动 `NabuBootMenu.efi`，插入基础 FD，挂接原 BDS 入口并在其前显示菜单。配置使用上游 `CONFIG_FILE` 常量，统一读取 ESP 的 `\EFI\visor\boot.conf`；图标和背景继续从 `\EFI\visor\` 加载。基础 FD 的其他可执行模块保留原字节。

横屏、音量键切换系统及电源动作、电源键松开确认是 nabu 适配。默认首次 5 秒无输入自动启动 Android，输入后取消本次倒计时。功能启用范围仅遵循上游 ARM64 架构和依赖限制。

横屏绘制使用 `src/visor_landscape.c` 与 `port/visor/NABU.patch` 中的坐标访问钩子。界面尺寸、菜单与触摸坐标仍是横屏；绘图缓冲区按物理屏幕顺序存储。视频背景沿用上游解码画布、最近邻缩放映射、透明像素规则与调暗色表，在一次绘制中写入旋转后的位置；相邻输出像素取自同一源像素时复用结果。提交画面只复制对应区域，不再转置整屏。图标、文字、模糊、开场画面及局部缓存恢复使用同一坐标映射，淡入淡出与全屏缓存复制保留原流程。

物理 GOP 提供有效的 RGB/BGR 32 位线性显存、容量与行跨度匹配，并且 CPU Architectural Protocol 的缓存同步可用时，直接提交并同步更新范围；不满足条件或缓存同步失败时使用原固件 GOP Blt，已按物理顺序绘制的画面仍无需再旋转。截图或交互录屏按需把当前画面与有效场景缓存转回横屏行序，本次菜单会话随后沿用原行序路径；启动全过程录屏启用时从一开始使用原行序路径。截图、录屏格式及视频解码、帧调度、配置和启动逻辑保持原样。

`tests/test_landscape.c` 使用主机模拟 GOP 与缓存接口，检查合并绘制与原缩放/调色规则的像素一致性、透明像素、重复映射与分块边界、非对齐局部刷新、行填充、RGB/BGR 色序、截图缓存转回、越界请求及缓存失败回退。新镜像的实际播放流畅度仍需真机确认；源码路径优化不等同于已经达到指定帧率。

## A/B 槽位维护

`boot_slot_build.py` 直接编译固定版本 Renegade 的 `BootSlotDxe.c` 与原版 `BootSlotLib`，生成独立 `BootSlotDxe.efi`，不将槽位算法链接进 Visor。源文件与协议声明通过 Git blob 校验；`port/boot-slot/` 仅提供 EDK2 库函数、协议 GUID、打包设备路径结构的 ABI 桥接及 EFI 入口。

FFS GUID 保持上游 `BA325AC5-10AB-44E3-B6C1-5BD273A3BF2D`，依赖表达式保持原版 `TRUE`。驱动插入菜单所在 DXE 固件卷，原 FD 已包含相同 GUID 时保留原模块，不重复添加或覆盖。每次进入 UEFI 执行原版 BootSlotMain：识别存储和 A/B 布局，对尚未 successful 或被标为不可启动的槽位清除不可启动标志并补满重试次数，再刷新当前槽位的优先级、重试次数并设置 successful，同时降低另一槽位的优先级、清除其 active 标志。未支持的存储或非 A/B 布局跳过处理。当前槽位为空时按上游回退 A；具体识别和写入仍由原 BootSlotLib 完成，不新增 boot-time BCB 检测。

槽位属性写入 GPT，并更新相应主、备表 CRC；原库也会核对当前槽位与 UFS Boot LUN，发现不一致时按原版规则纠正 Boot LUN。不更换内核、Recovery、misc 请求或 Secure Boot 策略。该驱动标记的是进入 UEFI 的槽位，不代表已经验证某个操作系统启动成功；原版也会清除另一槽位不可启动标志，不能把它理解为对另一槽位镜像有效性的检查。驱动只在 FD 能执行时工作，不能救回此前已被 bootloader 拒绝、无法进入 UEFI 的槽位。

主机模拟测试 `tests/test_boot_slot.c` 执行原版驱动与库，检查当前槽位为 A 或 B 时 UFS LUN 4 的主备 GPT 更新、CRC、其他分区与 LUN 字节保留、重复执行和非 A/B 跳过。上游低层 GPT 写入错误并非都能向 BootSlotMain 完整传播，本移植保留其处理方式，不额外改写槽位策略。许可及对应源代码见 `port/boot-slot/NOTICE.txt` 和固定版本 Renegade 子模块。

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

### 关机充电入口

磁吸版 ShellCode 在选择 Android 时直接继续原内核，不经过菜单或额外重启，因此保留 bootloader 的关机充电环境。菜单版在同一原生入口检查完整的 `androidboot.mode=charger` 参数；有效 nabu FDT 同时包含 `androidboot.hardware=qcom` 时，直接清除一次性标记并继续原内核，即使存在 `androidboot.force_normal_boot=1` 也优先处理充电启动。

此路径不启动 Visor、不执行五秒倒计时、不写 BCB、不热重启，保留 x0～x3、FDT、ramdisk 与原 Android 的充电程序。低电量策略、充电画面和电源键行为由原系统处理；不按 USB 是否连接或任意电量阈值判断，以免影响插电时正常开机。无充电参数、无效 FDT 或其他设备继续原有入口规则。当前识别限于 nabu bootloader 的命令行参数，不解析 bootconfig。

## Windows 与普通 EFI 条目

Android 配置 `nabu_action=android`，Windows 配置 `nabu_action=bds`。旧配置缺少这些键会进入普通 EFI 加载路径；升级时须检查实际 ESP 的 `boot.conf` 并替换配套 Android EFI，保留个人外观设置。

Windows 的 BDS 动作退出 Visor 并继续原固件启动流程。配置中的 `kernel=...bootmgfw.efi` 用于展示/识别，不是该动作的实际 `LoadImage` 目标。固件默认启动项改变时，BDS 的启动结果也会改变。

普通条目不设置 `nabu_action`，沿用上游 EFI 加载器。浏览器或快照明确覆盖内核/EFI 路径时，本次优先使用上游加载器，不修改保存的动作条目。加载失败进入上游错误恢复界面，文字操作需要 USB 键盘。Linux 的 ARM64 EFI 程序或 EFI Stub 内核可走普通路径，但兼容性需实际验证；三系统应配置独立的 Windows 与 Linux 入口。

### Linux rEFInd 接入

新增 `NABU_ACTION_LINUX=3` / `nabu_action=linux`。Android、BDS 动作仍退出菜单交给原驱动；Linux 在原启动位置调用 `src/visor_linux.c`，失败继续上游错误恢复。浏览器/快照临时覆盖路径仍优先原加载器。

适配层找到外部 `EFI/visor/refind/bootaa64.efi`，定位实际内核分区并检查内核和可选 initrd；不同卷以 GPT 分区 GUID 写入 rEFInd 原生 volume 字段。生成 UTF-16 LE 配置 `nabu-selected.conf`，将原 refind.conf 的文本并入生成的主配置，保留主题 include 的一级关系，再覆盖为手动单条目立即启动。参数内引号遵循 rEFInd 的双引号转义；拒绝控制字符与过长配置。每次清除旧文件、写入、Flush、回读核对和关闭所有句柄后，调用原 Visor EFI 加载器，以固定分区句柄加载 rEFInd，LoadOptions 为 `refind -c nabu-selected.conf`。目标的参数/initrd 不会误传给 rEFInd 本身。

原版 rEFInd 无法处理 Visor 加密容器、LUKS 辅助注入和 hash pin；Linux 动作检测到这些设置会明确拒绝，不丢弃安全约束。普通 Visor 加载路径完整保留。`tests/test_linux.c` 使用主机文件系统模拟，覆盖跨分区、换系统、引号/中文参数、清除旧字段、写保护/短写/Flush/回读失败与句柄释放。

`prepare_linux.py` 从用户提供的 TheMojoMan efi.tar 提取完整 rEFInd 文件到独立目录，保留原版资源内容；仅修改指定 boot.conf 条目的 kernel/action，保留 cmdline/initrd、其他条目和外观。发布沿用备份与回滚工具。来源、原版二进制哈希和文件哈希写入 linux-manifest.json，许可证见 port/refind/LICENSE.txt。

不更换原 FD 驱动，仅 Linux 动作发送原 BDS 使用的 EndOfDxe 通知，不改 Secure Boot 变量、信任库或认证处理。仅在 FD 新菜单驱动中增加 Linux 动作，因此首次部署需要更新 boot。该链路仍不同于完整原 BDS→rEFInd 链路；当前两项阶段/系统表适配已在本机临时启动 Ubuntu 成功。

## 构建与发布

`port/visor/NABU.patch` 只应用到 Visor 的临时副本。`port/recovery/overlay.py` 额外生成三个覆盖文件：

1. `src/visor_platform.c`：第三个电源按钮调用 Recovery 请求。
2. `third_party/visor/src/gui/gui_panels.c`：显示 Recovery 标签，保留原图标。
3. `third_party/visor/src/core/main.c`：请求失败显示原因并取消倒计时。

构建校验上游源码、固件模块保留、原内核主体与 ramdisk 保留，以及打包后内核匹配。BCB 模拟存储检查覆盖修改范围、512/4096 字节块、对齐、失败恢复和标记调用顺序。

全部成品和 ESP 资源先在临时目录生成并校验，再由 `build_publish.py` 发布。已有目录允许覆盖同名生成文件，无关文件保留。替换前文件备份到输出目录 `.build-backup/backup.zip`；`publication.json` 记录原路径。发布失败尝试恢复已替换文件，构建失败保留原成品。测试覆盖成功覆盖、准备失败及部分发布后的回滚。

三个依赖用子模块固定版本，工作树保持干净。首次构建自动生成功能头文件与清单，缺少的默认资源从 Visor 子模块准备到 ESP 副本，并生成 Android EFI。原始固件、工具安装和输出镜像不提交 Git。

### Linux 交接记录

用户要求关闭日志后，默认 `NABU_LINUX_TRACE=0`，不会打开、删除或写入交接日志，也不执行诊断变量读取；Linux 激活状态独立保留，确保系统表适配继续执行。生成的 rEFInd 主配置设置原生 `log_level 0`，Visor 配置设置 `file_log=0`。以下记录机制仅用于显式开启诊断时：Linux 动作在实际 rEFInd 所在 ESP 生成 `EFI/visor/refind/nabu-handoff.log`。每次尝试先删除旧记录，然后记录配置核对完成、镜像读取、固件 LoadImage/StartImage 前后及返回码；SecureBoot/SetupMode 仅读取。每行写入后 Flush 并关闭全部文件句柄，EFI 调用的参数、返回值、重试条件和认证处理保持上游原样。未选择 Linux 时记录不启用，写入失败不阻止原启动流程。最后一行只能定位已到达的阶段，不能证明下一阶段成功。rEFInd 自身仍使用原版日志。

经用户确认，Linux 交接在配置核对、释放句柄后，按原 BDS 的 `EfiEventGroupSignal` 方式发送 EndOfDxe 通知，再调用原 EFI 加载器。当前 FD 的 SecurityStubDxe 在该通知到达前会延迟外部镜像并返回 EFI_ACCESS_DENIED；真机记录已确认 rEFInd 未进入 StartImage。通知失败时返回原恢复界面，不绕过 LoadImage，不修改认证处理或安全启动变量。该候选只补齐 Linux 需要的阶段，仍未替代完整 BDS 初始化，当前后续系统表适配候选已临时启动 Ubuntu 成功，其他 Linux 目标兼容性须真机验证。

Linux 的原版 rEFInd 在 `UninitRefitLib` 中无条件比较 `ST->FirmwareVendor`。当前 FD 的原 BDS 才填写厂商字段；提前交接时为 NULL，已通过日志及二进制确认空指针风险。经用户确认，Linux 在 StartImage 前读取实际 LoadedImage.SystemTable，仅当厂商为空时用 RuntimeServicesData 补齐当前基础 FD 的原值 `Project Silicium`，版本为零时补 `0x10000`，并重算表 CRC。非空字段不覆盖，若 rEFInd 返回则恢复字段并重算当前表 CRC、释放内存；避免把子程序新增的配置表误还原。rEFInd 和驱动文件未修改，Secure Boot 变量及证书不变。此值匹配当前随附基础 FD，采用其他厂商 FD 时须重新核对元数据。

用户要求只生成关闭日志的镜像、暂不操作平板。Nabu 构建默认 `NABU_FILE_LOG=0`，Visor 的文件日志初始值和 setter 均关闭，旧 ESP 的日志配置不会恢复磁盘写入。正常上游构建不受此条件影响；开发诊断构建可显式启用该编译开关。rEFInd 的生成配置追加 `log_level 0`，覆盖 ESP 上先前开启的调试级别；不修改其程序和外部主题。平板上的旧日志与配置未删除或改写。
