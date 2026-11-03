# 构建说明

构建入口参考：`tools/build/release-original.sh`。通过 `BH3_BUILD_ROOT` 指定构建工作区，并按脚本约定准备源码、依赖前缀和输出目录。

## 依赖

| 依赖 | 要求 |
| --- | --- |
| 工具链容器 | 默认使用 manifests/dependencies.lock.json 中的固定 digest，可通过 TOOLCHAIN_IMAGE 覆盖 |
| android32 | 源码及 runtime.mk、NPDM 模板、链接脚本 |
| libnx32 / Mesa | 准备与工具链匹配的 ARM32 构建前缀 |
| FFmpeg | 7.1.1、ARM softfp、fno-short-enums，包含所需解码组件 |
| CPython / NumPy / zlib | 版本见 poc/python32_probe/dependencies.json，交叉编译材料位于同目录及 tools/ |
| NSP 封装 | 另行准备 hacBrewPack、hactool 及所需密钥 |

构建脚本读取匹配的ARM32依赖前缀，具体布局和各依赖的生成步骤见[开发接入指南](developer-guide.md)。依赖前缀由使用者在构建工作区准备。

先准备支持`linux/amd64`的Docker环境，并获取依赖锁定文件中的工具链镜像。主程序构建脚本使用本机镜像，不会自动拉取。

```sh
docker pull ghcr.io/vita2hos/devcontainer/vita2hos@sha256:3860772020f697865b0476ef72d680230b6b4f40d3f84659f546159b32f859d6
```

## 构建配置

目标配置启用 standalone、embedded server、AVPro 和 CG decode，table worker 模式为 6。完整参数见构建入口脚本。

建议先运行 `examples/minimal-app/` 验证 AArch32 与图形环境，再接入客户端、资源和服务端。示例及构建入口中的相对路径需与实际目录对应。

## 封装

`tools/packaging/` 提供安装包与卡带入口封装参考。使用前需按实际输入配置程序身份、图标、资源目录和外部工具位置。资源清单与程序内的资源身份必须匹配。

目录布局、生成文件及封装输入见 [开发接入指南](developer-guide.md)。

FFmpeg 目标构建入口：`LIBNX32=<prefix> sh tools/ffmpeg32/build-target.sh`。输出位于 tools/ffmpeg32/prefix；供主程序使用时将其配置或放置到构建工作区的 ffmpeg32-src/prefix。
