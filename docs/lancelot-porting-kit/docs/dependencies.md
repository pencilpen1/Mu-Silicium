# 依赖版本与本地 EDK2 修改

记录日期：2026-10-17。根仓库管理平台源码、工具、文档、分析材料和补丁；
外部 Git checkout、ROM 分区及构建输出由根目录 `.gitignore` 排除。

## 当前 checkout

| 路径 | 上游 | 当前 commit |
| --- | --- | --- |
| `ws/edk2` | https://github.com/tianocore/edk2.git | `f80f052277c88a67c55e107b550f504eeea947d3`（edk2-stable202302） |
| `ws/edk2-platforms` | https://github.com/tianocore/edk2-platforms.git | `1c500d13a653cf0f556ecf7ce703c12874247efc` |
| `refs/edk2-mt6765` | https://github.com/xiaomi-blossom-dev/edk2-mt6765.git | `debee5b0ff6f504ff2264cee980f95c5293b4fa6` |
| `refs/edk2` | https://github.com/tianocore/edk2.git | `c1415771ac592ad8509351c46cd3cda4f38d7d52` |

实际构建使用 `ws/` 下的依赖；`refs/` 是参考材料。
EDK2 顶层 submodule 已初始化并与该 commit 匹配，OpenSSL 内部的可选测试 submodule 未初始化。

## 必须保留的 EDK2 修改

`ws/edk2` 有两个已跟踪文件的本地修改：

- `MdeModulePkg/Core/Dxe/DxeMain.inf`：加入 MT6768Pkg、SerialPortLib 和 BootLogLib 依赖。
- `MdeModulePkg/Core/Dxe/DxeMain/DxeMain.c`：加入 D00–D09 屏幕阶段标记，并提前初始化 framebuffer 日志，帮助定位 DXE 早期异常。

这些修改保存于 [`patches/edk2-lancelot-dxe-probes.patch`](../patches/edk2-lancelot-dxe-probes.patch)。
补丁基于上表的 EDK2 commit。当前本地 checkout 已有这些修改；在**新建的干净 checkout** 上应用：

```sh
git -C ws/edk2 apply --check --ignore-whitespace ../../patches/edk2-lancelot-dxe-probes.patch
git -C ws/edk2 apply --ignore-whitespace ../../patches/edk2-lancelot-dxe-probes.patch
```

`--ignore-whitespace` 用于匹配上游 CRLF 与本地混合行尾的上下文。
以后修改外部 EDK2 文件时，需要同步保存补丁；根仓库的 `git status` 不会列出被忽略 checkout 内的变化。

## 新环境准备

工具：Git、Python 3、make、宿主 GCC、AArch64 GNU 工具链（`aarch64-linux-gnu-`）、
iasl、NASM，以及 EDK2 BaseTools 所需的开发依赖。DT 分析还使用 dtc。

在依赖目录尚不存在的新环境中，从项目根目录运行：

```sh
git clone https://github.com/tianocore/edk2.git ws/edk2
git -C ws/edk2 checkout f80f052277c88a67c55e107b550f504eeea947d3
git -C ws/edk2 submodule update --init
git clone https://github.com/tianocore/edk2-platforms.git ws/edk2-platforms
git -C ws/edk2-platforms checkout 1c500d13a653cf0f556ecf7ce703c12874247efc
git -C ws/edk2 apply --ignore-whitespace ../../patches/edk2-lancelot-dxe-probes.patch
cd ws
. ./build_common.sh
make -C edk2/BaseTools
./build.sh
```

`ws/firstrun.sh` 仍保留旧布局的 `../edk2/BaseTools` 路径；上面的 `edk2/BaseTools`
是当前目录结构对应的路径。`tools/fork_pkg.sh` 为最初派生平台包的一次性脚本，
包含本机绝对路径，不是当前源码的日常构建入口。

构建输入 `ws/blobs/dtb` 和 `ws/blobs/initramfs` 保留在项目树内。
离线镜像审计另需 `rom-extract/boot.img`，应从本项目使用的 ROM 提取并保存在本地；
其余原始分区与生成的 `boot-uefi*.img` 也不纳入源码仓库。
