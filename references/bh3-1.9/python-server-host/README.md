# Python 服务端托管参考

`nx_server.py` 提供基于 `_nxipc` 的服务端托管模板，不包含业务实现。使用前实现 `nx_backend`，并验证业务后端与内存通道的完整游戏请求。示例仅演示HTTP接口。

实现 `nx_backend.configure(runtime, data)`，返回三项：

1. `game_handler(socket, address, server)`：处理完整游戏会话。
2. `http_handler(socket, address, server)`：处理 HTTP 请求。
3. `verified_files`：完成初始化与输入校验的文件数量。

HTTP socket 提供 makefile 和 sendall；游戏 socket 提供 settimeout、recv 和 sendall。业务处理器需要的其他方法需自行适配。

`runtime` 为 Python 根目录下的 runtime 子目录，`data` 为服务端根目录下的 data。目录内容、协议、持久化及业务初始化需由后端实现；ready 必须在就绪后通知。线程退出由进程生命周期统管，异常会关闭对应内存通道。



`nx_backend.example.py` 是不含游戏协议的 HTTP 示例。复制为 nx_backend.py 可验证托管入口，游戏请求需要另行实现。
