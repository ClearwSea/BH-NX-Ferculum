# 功能与接入要求

## 组件职责

| 组件 | 提供的能力 | 使用时准备 |
| --- | --- | --- |
| Android AArch32运行时 | 原生库装载、系统接口及平台适配 | 匹配工具链的ARM32依赖 |
| 指定客户端兼容层 | Unity/IL2CPP特殊补丁、输入及音视频桥接 | 匹配的客户端构建、原生库与资源 |
| 嵌入式Python托管 | `_nxipc`内存通道、HTTP与游戏会话分发 | `nx_backend`后端实现、业务模块及数据 |
| 资源安装器 | 清单校验、资源身份及已有存档保护 | 外部资源载荷、manifest.tsv与package.id |
| 构建与封装工具 | 主程序构建、NSP与卡带入口生成 | ARM32依赖前缀及所需外部工具 |

## 运行条件

- 客户端补丁依赖指定二进制布局；更换客户端构建时需检查补丁地址和指令前像。
- libnx32、Mesa、FFmpeg、CPython和NumPy需要使用与工具链匹配的ARM32构建，宿主机库不能替代。
- 服务端托管模板需要接入实际业务后端。`nx_backend.example.py`只返回HTTP演示响应，不实现游戏协议；外部后端与内存通道的完整游戏请求仍需按实际组合验证。
- 主程序启动字库需先由`tools/generate_boot_ui_font.py`从本地字体生成。
- 资源载荷改变后，重新生成manifest.tsv、package.id及程序内资源身份，再构建主程序。

构建布局及命令见[开发接入指南](developer-guide.md)，后端接口见[接入说明](integration.md)。
