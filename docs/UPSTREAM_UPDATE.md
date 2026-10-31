# 一键更新 Visor

ESP 应用分支在工程目录执行，显式指定允许未签名 EFI 的原始 NoSB 固件：

```powershell
py update_visor.py --fd "E:\path\nabu-NoSB.img"
```

需要 Python 3、Git for Windows 和能够访问 GitHub 的网络。Zig、补丁工具与原始镜像按 README 准备。

默认获取上游 `main` 的最新提交。如果当前已是这个 commit，则显示“无需更新”。发现新 commit 时，脚本会：

1. 在独立临时目录下载完整上游源码，记录准确 commit。
2. 检查并应用 `port/visor/NABU.patch`，重新生成完整 ARM64 功能清单。
3. 沿用 nabu、Renegade 标记和 PSCI 桥接、Recovery 按钮，编译菜单与 Android EFI、boot 和 ESP 包；输入 FD 原样保留，不编译或注入 DXE。
4. 将被替换的文件备份到 `.visor-update/backup.zip`，然后将 Visor 子模块检出到新 commit，更新功能清单、ESP 资源、来源记录和 `dist/` 中的同名成品。GNU-EFI 与 Renegade 的子模块提交保持固定。

**脚本不刷机、不挂载或修改平板 ESP。** 更新后的镜像需要手动刷入、复制配置包并确认 Android、Windows、Recovery。构建通过不等于真机已经验证；新 manifest 会明确记录未真机验证状态。

## 常用命令

仅下载、打补丁和编译检查，保留当前源码、配置与成品：

```powershell
py update_visor.py --fd "E:\path\nabu-NoSB.img" --check-only
```

更新到指定 tag、分支或完整 commit：

```powershell
py update_visor.py --fd "E:\path\nabu-NoSB.img" --ref 71a05b5945e37903cfeeaf69c76220fdf5861ac9
```

同时更换原始 boot、FD 或输出目录：

```powershell
py update_visor.py --boot "E:\你的文件夹\boot.img" --fd "E:\你的文件夹\nabu-NoSB.img" --out-dir ".\dist"
```

输出目录允许已存在，同名生成文件覆盖，无关文件保留。更新脚本的输出目录须在工程内，并避开源码和依赖目录；普通 `build.py` 没有这项更新事务的路径限制。
原始输入必须是未加菜单补丁的 Android boot 与 nabu NoSB 固件。`--fd` 不能省略；`--boot` 省略时使用 `tools/official/stock-boot.img`，不自动猜测上次手动构建的输入。

本地上游源码也可使用；它必须未打过 nabu 补丁。目录含 Git 记录时要求工作区干净；源码包无 Git 记录时要指定完整 commit：

```powershell
py update_visor.py --fd "E:\path\nabu-NoSB.img" --source "E:\你的文件夹\Visor-BootManager" --ref 71a05b5945e37903cfeeaf69c76220fdf5861ac9
```

## 保留配置与处理失败

- `config/boot.conf` 和 `esp/EFI/visor/boot.conf` 各自保持原内容。配置包使用后者；如需把 config 的修改打包，仍按 README 先同步。
- 新的原版图标、背景、素材与配置示例会同步到 ESP 副本。已被你修改的素材及自己添加的文件保留。平板 ESP 上的个人修改还需自行备份并合并，脚本读取的是电脑工程副本。
- 上游补丁冲突、Recovery 覆盖器接口变化或编译失败时停止，不替换当前源码和成品。不会跳过适配、裁剪功能或自动改写启动逻辑。
- 检测到 `third_party/visor/` 的本地修改时停止，避免把你的改动覆盖掉。人工维护者应把适配维护在 `port/visor/NABU.patch`，而不是直接修改子模块。初始克隆使用 `git submodule update --init --depth 1`。
- 发布阶段失败时尝试恢复原文件；文件锁或进程中断可能阻止自动恢复。被替换的主仓库文件在 `.visor-update/backup.zip` 中，按压缩包内原路径恢复到工程；Visor 子模块可检出到备份内 `port/visor-upstream.json` 记录的旧 commit。`_update_backup.json` 记录哪些文件原先不存在。
- 备份只保留最近一次，下一次成功构建并进入发布时覆盖。需要长期保留某个成功镜像时另外备份。
- 错误详情在 `.visor-update/last-error.log`。更新锁避免并行运行；异常退出留下锁时，确认没有更新进程后再删除 `.visor-update/update.lock`。

脚本可自动处理补丁能应用且现有工具链能编译的更新。上游架构、接口、依赖或配置语义改变时可能需要人工移植；按项目原则，新的行为差异须先说明并取得用户决定。补丁能应用、编译通过也不能证明所有新行为都兼容，仍需核对更新内容和真机表现。

## 来源与人工维护

初始上游基线为 `71a05b5945e37903cfeeaf69c76220fdf5861ac9`。当前准确版本、补丁哈希和源码校验值以 `port/visor-upstream.json` 为准；新的镜像 manifest 也记录这些信息。

`NABU.patch` 集中维护菜单硬件接口、ABI、Linux 动作及其余上游适配，不包含旧 Android/BDS 动作解析或截获。普通 EFI 条目由上游加载，配置缺失沿用上游自动发现。`port/recovery/overlay.py` 处理左下角 Recovery 按钮与错误显示，不生成独立 Recovery EFI。Renegade 写入/接收循环仍来自固定子模块，仅私有运行时服务桥接改为 PSCI 复位。

上游项目：[IO-ZetZor/Visor-BootManager](https://github.com/IO-ZetZor/Visor-BootManager)。不要用未经 nabu 适配的上游二进制覆盖本分支应用；应通过本脚本重新应用平台补丁和编译。仅更新菜单/主题时安装 ESP 文件即可，FD、内核或分流器变化时才需刷入新 boot。

更新后主仓库会出现 Visor 子模块指针、来源记录和配置资源的变化。需要把新版本保存到 GitHub 时，检查后提交并推送这些变化；更新脚本不自动提交或推送。所有适配仍仅在临时副本应用。
