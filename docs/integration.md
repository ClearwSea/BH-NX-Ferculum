# 接入说明

## 客户端

客户端 APK、ARMv7 原生库和资源放在仓库外。`poc/m3_boot/source/port_config.h` 定义目标包名及 SD 路径，调整时同时检查资源安装、网络桥接和服务端目录。

特殊补丁位于同一源码目录，包含固定地址和指令检查。应先核对目标库哈希，再决定是否沿用；版本号相同不能代替二进制匹配。

## 服务端

`local_server.c` 提供嵌入式 Python 与 `_nxipc` 内存通道。`references/bh3-1.9/python-server-host/` 提供启动入口模板。接收者提供 `nx_backend` 及自己的业务模块，并将兼容入口部署到配置的服务端根目录。

## 外部Python服务端

可以自行实现业务后端，也可研究[已公开的BH2017-LOCRENASCI服务端源码](https://github.com/ClearwSea/BH2017-LOCRENASCI/tree/v0.1.0)，在仓库外适配为`nx_backend`。

接入时对接数据初始化、游戏会话处理器与HTTP处理器，并检查协议版本和资源布局。BH2017-LOCRENASCI的`server19_portable.configure`、`server19.GameHandler`和`server19_http.make_handler`可作为阅读入口；其`start`函数创建TCP/HTTP监听端口，NS内存通道需要使用本项目的托管入口。还需按客户端网络桥接配置返回的地址和端口。

## 资源与封装

游戏输入、服务端业务和初始数据由接收者自行组织。安装器使用 manifest.tsv 与 package.id 校验输入；原发行身份不能用于新资源包。

封装时需配置输入目录、程序身份、图标和外部工具路径。游戏内容、初始数据及密钥由使用者自行准备。
