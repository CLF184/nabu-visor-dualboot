# 一键更新 Visor

双击工程根目录的 **`update_visor.cmd`**。也可以在 PowerShell 中进入工程后执行：

```powershell
py update_visor.py
```

需要 Python 3、Git for Windows 和能够访问 GitHub 的网络。Zig 与补丁工具沿用工程自带依赖。

默认获取上游 `main` 的最新提交。如果当前已是这个 commit，则显示“无需更新”。发现新 commit 时，脚本会：

1. 在独立临时目录下载完整上游源码，记录准确 commit。
2. 检查并应用 `port/visor/NABU.patch`，重新生成完整 ARM64 功能清单。
3. 沿用现有 nabu、Renegade 和 Recovery 适配，编译 FD、boot 镜像和 ESP 包，执行现有构建校验。
4. 将被替换的文件备份到 `.visor-update/backup.zip`，然后将 Visor 子模块检出到新 commit，更新功能清单、ESP 资源、来源记录和 `dist/` 中的同名成品。GNU-EFI 与 Renegade 的子模块提交保持固定。

**脚本不刷机、不挂载或修改平板 ESP。** 更新后的镜像需要手动刷入、复制配置包并确认 Android、Windows、Recovery。构建通过不等于真机已经验证；新 manifest 会明确记录未真机验证状态。

## 常用命令

仅下载、打补丁和编译检查，保留当前源码、配置与成品：

```powershell
py update_visor.py --check-only
```

更新到指定 tag、分支或完整 commit：

```powershell
py update_visor.py --ref 71a05b5945e37903cfeeaf69c76220fdf5861ac9
```

同时更换原始 boot、FD 或输出目录：

```powershell
py update_visor.py --boot "E:\你的文件夹\boot.img" --fd "E:\你的文件夹\nabu.fd" --out-dir ".\dist"
```

输出目录允许已存在，同名生成文件覆盖，无关文件保留。更新脚本的输出目录须在工程内，并避开源码和依赖目录；普通 `build.py` 没有这项更新事务的路径限制。
原始输入必须是未加本菜单补丁的 Android boot 与 nabu FD。省略时使用 `tools/official/` 中的默认输入，不自动猜测上次手动构建用了什么输入。

本地上游源码也可使用；它必须未打过 nabu 补丁。目录含 Git 记录时要求工作区干净；源码包无 Git 记录时要指定完整 commit：

```powershell
py update_visor.py --source "E:\你的文件夹\Visor-BootManager" --ref 71a05b5945e37903cfeeaf69c76220fdf5861ac9
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

`NABU.patch` 覆盖当前已验证的 12 个上游文件适配，包含 `nabu_action` 配置解析、Android/BDS 交接、菜单硬件接口和 ABI 适配。`port/recovery/overlay.py` 继续处理 Recovery 按钮标签与请求错误显示；`port/renegade/` 继续提供已经验证的 Android 标记配对实现。

上游项目：[IO-ZetZor/Visor-BootManager](https://github.com/IO-ZetZor/Visor-BootManager)。上游 `visor update` 适用于其原版 ESP 安装方式，不更新本工程 FD 内的菜单。

更新后主仓库会出现 Visor 子模块指针、来源记录和配置资源的变化。需要把新版本保存到 GitHub 时，检查后提交并推送这些变化；更新脚本不自动提交或推送。所有适配仍仅在临时副本应用。
