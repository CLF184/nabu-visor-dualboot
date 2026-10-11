# 当前 nabu 移植实现

本机已有反馈：Android 标记配对方案成功；Windows 原始 BDS 流程成功；用户于 2026-10-10 确认 Recovery 按钮可直接进入 Recovery。2026-10-11 移除开机 BCB 自检并修复平板旧配置和旧 Android EFI 后，最新镜像刷入 boot_a，用户确认一切正常。Linux 尚未在本机验证。

## 菜单与固件

Visor 编译为 AArch64 DXE 驱动 `NabuBootMenu.efi`，插入基础 FD，挂接原 BDS 入口并在其前显示菜单。配置统一从 ESP 的 `\EFI\visor\boot.conf` 读取，使用上游 `CONFIG_FILE` 常量；图标、背景继续从 ESP 的 `\EFI\visor\` 加载。基础 FD 的其他可执行模块保留原字节。

横屏、音量键轮流选择系统和电源动作、电源松开确认是 nabu 适配。初始 5 秒无输入自动 Android，输入后取消本次倒计时。构建按上游完整功能清单处理，只遵循其 ARM64 架构限制。

## Android 标记配对

Renegade 源码固定为 `e1952621f419f8db60ed28271264e1b5184c571d`。上游 `Reboot2PayloadApp.c` 从 `third_party/renegade/` 子模块直接读取，构建校验 Git blob。

写入端将 `0x17FE00000` 的 128 个半字设为 `0x5241`，写回并失效全部 256 字节缓存，调用 `EfiResetWarm`。EDK2 到 GNU-EFI 的适配只有运行时表和库函数桥接；缓存行大小从 `CTR_EL0` 读取。

接收端沿用上游 `BootShim.Dualboot.S` 的扫描和清除循环，与原厂 Recovery 识别代码一起追加在 FD 后（当前合计 992 字节）。任意标记匹配时，清空全部 256 字节，再跳回原内核；未匹配时接回官方 Nabu2 的 FD 复制入口。原 Android 内核 `0xb0` 后的主体、原 FD 入口约定和解压后的 ramdisk 保留。

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

### 原厂 Recovery 启动

2026-10-11 对照采集：用户正常进入 Android 时，`ro.boot.force_normal_boot=1`；在旧磁吸版 boot 中通过电源＋音量上进入 Recovery 时，原厂 FDT 的 `/chosen/bootargs` 不含 `androidboot.force_normal_boot=1`。随后在同一旧磁吸版上实际执行 `fastboot reboot recovery`，采集结果也不含该参数。两份真实 FDT 均通过候选入口的 AArch64 模拟检查；正常启动参数、原标记协议与无效 FDT 兜底也通过检查。这是 recovery-as-boot 的启动约定，见 [AOSP ramdisk 文档](https://source.android.com/docs/core/architecture/partitions/ramdisk-partitions)。Recovery 抓取中 BCB command 已为空，不能据此判断 bootloader 先前是否读取或修改过 BCB。

当前磁吸 boot 的入口先检查硬件状态，再选择原内核或 FD。它没有独立的按键 Recovery 加载器；跳回原内核时保留原厂传入的启动环境。FD 交接代码会覆盖 x0/x1，因此新增判断放在该交接之前，而非 Visor 菜单中。

`port/recovery/native_entry.S` 在原版 Renegade 标记扫描未命中后，只读检查 x0 指向的 FDT。仅在 FDT 版本、范围、节点与字符串边界有效，根 model 为 `NABU`，`/chosen/bootargs` 有 `androidboot.hardware=qcom` 且没有正常启动参数时，使用上游清除标记循环并跳回原内核。它保留 x0～x3、原 FDT 与 ramdisk，不使用栈，不写 BCB，不重启。正常启动或数据无效/不匹配时仍进入原 FD/Visor。此判断针对当前 nabu 命令行协议，不解析其他内核的 bootconfig。

由此保留原厂电源＋音量上 Recovery 启动环境；原厂已为 `fastboot reboot recovery` 或 `adb reboot recovery` 准备相同环境时也可直接继续。用户已确认上一份候选的电源＋音量上入口有效；后续生成的镜像仍需核对实际启动结果。

按用户要求移除 DeepSeek 增加的菜单前 BCB 检查；开机不再读取 misc 请求并自动热重启。菜单中的 Recovery 按钮仍保留写入请求、标记与热重启流程。

## Windows 与 Linux

旧版 ESP 配置迁移时必须在实际 `boot.conf` 补齐 Android 的 `nabu_action=android` 与 Windows 的 `nabu_action=bds`。2026-10-11 实机读取发现缺少这两个键，且外部 Android EFI 为旧版本；这会选择普通 EFI 加载路径，绕过已验证的标记桥接与 Windows BDS。更新时保留用户外观设置。补齐配置、更新 EFI 并刷入去除开机 BCB 自检的新镜像后，用户确认一切正常。

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
