# 开发接入指南

## 构建工作区

```text
<build-root>/
  BH-NX-Ferculum/         本仓库
  libnx32/prefix/         已编译的 libnx32
  mesa32/prefix/          已编译的 Mesa
  ffmpeg32-src/prefix/    ARM32 FFmpeg
  python32-probe/         Python 与 NumPy 构建工作区
```

libnx32 与 Mesa 的源码 URL 和提交见 `manifests/dependencies.lock.json`，在对应提交执行上游 `build.sh`，并使用同一工具链镜像。Mesa 构建使用上述 libnx32 前缀。需要 ETC 软件回退时，可参考 `tools/patch_mesa_etc.py`。

FFmpeg 需要 libavformat、libavcodec、libavutil，使用 ARM softfp 与四字节枚举，启用 MOV、H.264、AAC 组件。目标配方见 tools/ffmpeg32/build-target.sh。该入口配置 MOV、H.264 与 AAC；脚本会下载并校验 FFmpeg 7.1.1 源码。设置 LIBNX32 指向 ARM32 前缀，构建后将生成的 prefix 配置到 ffmpeg32-src/prefix。不能用宿主机库代替。

## 最小示例

在上述工作区设置根目录，并明确指定外部依赖前缀：

```sh
export BH3_BUILD_ROOT=/path/to/build-root
LIBNX32="$BH3_BUILD_ROOT/libnx32/prefix" \
PORTLIBS="$BH3_BUILD_ROOT/mesa32/prefix" \
RUNTIME_DIR="$BH3_BUILD_ROOT/BH-NX-Ferculum/third_party/android32" \
sh "$BH3_BUILD_ROOT/BH-NX-Ferculum/examples/minimal-app/build.sh"
```

示例生成AArch32图形程序，不读取客户端或服务端；在设备上显示颜色画面并输出运行日志。

## Python 与 NumPy

准备 `poc/python32_probe/dependencies.json` 中列出的源码归档并校验 SHA-256。

1. 在 Python 工作区运行 `tools/build_python32_probe.sh <python-workspace>`。脚本需要容器内的 ARM 工具链与 libnx32。
2. 将 NumPy 源码解压到 `numpy-1.26.2/`，Cython 解压到 `build-tools/cython-3.0.12/`。
3. 将 numpy-cc、numpy-cpp、numpy-python、cython 放入 `build-tools/` 并授予执行权限，将 numpy-cross.ini 放到 Python 工作区根目录。
4. 在 `/py` 挂载 Python 工作区、`/nx` 挂载 libnx32 前缀的容器中运行 configure_numpy.sh、build_numpy.sh，再执行 numpy_static.py。
5. `numpy-static/numpy_inittab.h` 与 `objects.rsp` 是生成文件，必须在主程序编译前生成。`dcr_build.h` 由 runtime.mk 自动生成。

运行时需要 Python 标准库、NumPy Python 模块及服务端依赖。`package_numpy_python.py --root <python-workspace> --output <archive>` 可整理 NumPy 的 Python 模块；其他标准库及业务依赖由使用者准备。

## 启动文字字库

启动界面的`boot_ui_font.h`是生成文件。准备支持简体中文的字体，在仓库根目录运行：

```sh
python3 -m pip install Pillow
python3 tools/generate_boot_ui_font.py --font /path/to/font.ttf
```

可使用[Noto Sans SC](https://fonts.google.com/noto/specimen/Noto+Sans+SC)，其许可为[OFL-1.1](https://github.com/google/fonts/blob/main/ofl/notosanssc/OFL.txt)。字体文件由使用者准备；生成结果不进入Git。重新分发字库时保留所选字体要求的版权与许可。

先生成字库，再构建`poc/m3_boot`主程序。无游戏内容的最小图形示例不依赖这份字库。

## 运行目录

默认 standalone 配置读取：

```text
/switch/bh3_2017/
  game.apk
  libmain.so / libulua.so / libunity.so / libil2cpp.so
  libAkSoundEngine.so / libAVProLocal.so
  python/stdlib/          Python 标准库
  python/runtime/         接收者的运行依赖
  server-stage3/          nx_boot_profile.py、nx_server.py、nx_backend.py
  server-stage3/data/     接收者的数据目录
```

资源目录布局必须与客户端读取路径匹配。具体路径见 port_config.h、运行时 dcr_path.c 及资源桥接代码。游戏文件、业务依赖和数据不随仓库提供。

服务端模板见 references/bh3-1.9/python-server-host/。`nx_backend.example.py` 仅演示 HTTP 处理器契约，返回 501，不实现游戏协议。

## 资源清单与主程序

将自行准备的内容放到 `<romfs>/payload/`，输出清单前确定哪些文件属于只在缺失时初始化的 S 类数据。已有数据目录由使用者保管。

```sh
python3 tools/resources/prepare_manifest.py --romfs <romfs> --header <new-header> --seed server-stage3/data/account.json
```

不需要初始化数据时省略 `--seed`。输出文件必须不存在；新头文件放在载荷目录之外。将其用于 `poc/m3_boot/source/package_identity.h`，再编译主程序。修改资源后应重新生成清单及头文件。

```sh
export BH3_BUILD_ROOT=<build-root>
bash tools/build/release-original.sh
```

该步骤依赖全部构建前缀已准备好。生成的 release.nsp 是 ExeFS 容器，不是完整安装包。

## 完整安装包与卡带入口

完整封装需要以下外部文件：

```text
<package-root>/digital-local.nsp             主程序 ExeFS 容器
<package-root>/digital-package/control/      control.nacp 与 icon_AmericanEnglish.dat
<package-root>/tools/hacBrewPack/hacbrewpack  外部打包工具
<package-root>/tools/hactool/hactool          外部校验工具
```

控制文件中的显示名称和身份需要与封装脚本检查一致，调整目标名称时同步修改对应检查。工具来源见 dependencies.lock.json 的 packaging_tools。所需 Python 版本为 3.12 或更新。

```sh
python3 tools/packaging/build_standalone_nsp.py --root <package-root> --keys <external-keys> --title-id 01000C4015030000 --prepared-romfs <romfs>
```

主程序 NPDM 必须使用相同 title ID。卡带入口可通过 prepare_gamecard_entry.py 的 `--exefs`、`--npdm`、`--title-id`、`--output` 生成；输出目录必须不存在。只允许 NPDM 的三个身份字段发生变化，运行程序保持一致。外观模块和图标不是运行该兼容层的必需部分。
