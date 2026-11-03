[中文](README.md) · [English](README_ENG.md)

> 本 README 由 Agent 自动生成，内容请以实际情况为准。

# BH-NX-Ferculum

> **本项目是 VIBECODING 产物。** 开发过程中大量使用 AI 辅助，代码可能存在疏漏；如遇问题，请多谅解，欢迎开发者修复与改进。

面向崩坏3 1.9 Android ARMv7 客户端的 Nintendo Switch 兼容层与接入参考。

项目基于 android32、libnx32、Mesa 等开源移植工作，提供指定客户端的兼容修复、特殊补丁、输入与音视频桥接、Python服务端托管方式，以及资源部署和封装工具。使用者准备匹配的客户端、游戏资源与服务端，按接入接口配置自己的运行组合。

可以直接研究和修改源码，将仓库交给 Agent 协作开发，或使用构建脚本生成运行程序。NSP封装与卡带入口工具按需使用。开发入口见[开发接入指南](docs/developer-guide.md)和[开发约定](AGENTS.md)。

## 提供内容

| 内容 | 用途 |
| --- | --- |
| 平台运行与兼容层 | Android AArch32 装载、系统接口及平台适配 |
| 指定客户端补丁 | Unity / IL2CPP 启动、输入、音视频及资源读取适配 |
| 服务端托管与接入模板 | 嵌入式 Python、内存通信通道及后端接口 |
| 构建与部署工具 | 依赖构建脚本、资源安装与校验、NSP封装及卡带入口 |
| 最小示例 | 不加载游戏的 AArch32 图形程序 |

客户端、原生游戏库、游戏资源、服务端业务源码与数据、存档及密钥由使用者另行准备。启动文字字库由本地字体生成，步骤见[构建说明](docs/developer-guide.md#启动文字字库)。

## 版本与适用目标

基于 **2026年11月1日版本**，不代表最新开发进度，也不保证包含后续功能与修复。需要新功能或其他版本支持时，可在此基础上自行开发。

目标为崩坏3 1.9 / Unity 5.3.6p5 / IL2CPP / Android `armeabi-v7a`。专用补丁对应的 `libil2cpp.so` SHA-256：

```text
3c47c6774dcceee5d54b4cdcc6c2d4eb3f0da87cbbf8d0b0ee08a5729023b867
```

补丁绑定具体二进制布局；相同版本号的其他构建需要重新核对地址和指令特征。服务端需要实现托管接口，运行条件见[功能与接入要求](docs/status.md)。

### 已实测设备

以下设备由维护者实际运行测试，供参考；未列出的环境不保证相同表现。

| 设备 | 系统环境 |
| --- | --- |
| Nintendo Switch V1（HAC001） | 21.2.0\|AMS 1.10.2\|E |

## 使用流程

1. 按[构建说明](docs/build.md)准备固定工具链、ARM32依赖及工作目录。
2. 构建[最小示例](docs/developer-guide.md#最小示例)，在设备上确认AArch32与图形环境。
3. 在仓库外准备客户端、资源、服务端及其数据。
4. 按[接入说明](docs/integration.md)配置客户端路径与 `nx_backend` 后端入口。
5. 生成资源清单和程序内资源身份，构建运行程序并进行设备测试。
6. 根据使用方式封装NSP或生成卡带入口。

## 服务端接入

Python运行时嵌入主程序，通过内存字节通道承接游戏连接和HTTP请求，无需创建BSD监听端口。仓库提供托管机制与接口模板，服务端业务由外部实现。

`nx_backend` 负责初始化运行数据，并提供HTTP处理器和游戏会话处理器。示例后端只演示HTTP接口，不实现游戏协议。契约见[Python服务端托管参考](references/bh3-1.9/python-server-host/README.md)。

其他语言的服务端需要实现对应运行时、通信与生命周期管理。外部Python后端的接入位置见[接入说明](docs/integration.md#外部python服务端)。

## 目录

```text
poc/m3_boot/             指定目标的兼容与启动源码
third_party/android32/  运行时源码及构建支撑
references/bh3-1.9/     服务端托管与接入模板
poc/python32_probe/     Python / NumPy 交叉编译材料
tools/                  构建、资源校验与封装工具
examples/minimal-app/   无游戏内容的图形示例
manifests/              文件来源、依赖版本与文件清单
docs/                   构建、接入与功能说明
```

## 许可与致谢

本项目自有新增代码采用[MIT许可证](LICENSE)，版权署名 **ClearwSea**。第三方代码保留各自许可证和原作者声明，主要来源及范围见[第三方来源与许可](THIRD_PARTY_NOTICES.md)。游戏客户端、资源、商标和外部服务端不因本项目许可而获得额外授权。

运行基础来自 aks796 的 android32、libnx32、mesa32、dcr_sea_nx，Liartes 的 spacehulk-nx，以及 TheOfficialFloW、fgsfds、xerpi、switchbrew、devkitPro、Mesa 和相关上游开发者。另使用 Python、NumPy、FFmpeg 及构建封装工具。

感谢 **ChatGPT、DeepSeek、Claude、Gemini** 在开发、问题分析与代码迭代中的帮助。
