# 第三方来源与许可

下表列出主要第三方来源。许可适用范围以各组件及原文件声明为准。

| 来源 | 用途 | 当前记录 |
| --- | --- | --- |
| aks796/android32 | Android AArch32 运行基础 | MIT；保留 LICENSE 和文件头 |
| Liartes/spacehulk-nx、aks796/dcr_sea_nx | 移植参考与项目基础 | 保留相关上游署名 |
| TheOfficialFloW、fgsfds | 装载器来源 | 保留文件声明 |
| aks796/libnx32、switchbrew/libnx、xerpi/vita2hos | 系统接口与工具链 | 许可原文见 LICENSES/libnx32.txt |
| aks796/mesa32、devkitPro、Mesa | 图形运行基础 | 外部图形依赖；上游版本见依赖锁定文件，组件许可见其源码中的docs/license.html |
| FFmpeg | 视频解码 | MOV / H.264 / AAC 目标配置见 tools/ffmpeg32；许可材料见 LICENSES/ |
| CPython、NumPy、zlib | 嵌入式运行时 | 具体版本见依赖记录，许可原文见 LICENSES/ |
| rlaphoenix/hacBrewPack、SciresM/hactool | 外部封装与校验工具 | 不附工具二进制，使用者另行准备 |

上游版本记录见 manifests/dependencies.lock.json。本项目自有新增代码采用根目录[LICENSE](LICENSE)中的MIT许可证，版权署名ClearwSea；上表组件继续适用各自许可。

许可原文与适用组件的对应表见 [LICENSES/README.md](LICENSES/README.md)。
