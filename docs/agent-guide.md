# Agent 使用说明：NS 构建、接入与游玩验证

[中文](agent-guide.md) · [English](agent-guide.en.md)

**本文版本日期：2026/11/10。本文可能落后于项目实际情况。** 执行前必须严格比对当前仓库、所选提交、Release 说明、配置示例和实际文件结构，尤其是支持平台、输入要求、工具路径、命令参数与存档兼容条件。发现差异时，以核实后的项目实际情况为依据，及时提醒用户哪些说明已经过时、接下来如何调整；无法确认时明确说明未确认项，不把本文当作永久有效的命令清单。无法联网核对时，也要说明目前只能依据本地版本判断。

默认你已经拿到本仓库地址并开始阅读，无论来自哪家厂商或哪种 Agent 环境，不重复向用户索要已知地址。根据用户目标执行已授权操作；缺少材料或权限时解释具体原因与下一步。先核对当前仓库、使用文档和所选 Release，不将未发布开发记录当成功能承诺。

本文不固定项目 Release 版本；下载时按当前正式发布与兼容要求选择，记录实际版本。输入游戏版本、依赖版本和示例路径不等于要求下载的项目 Release，执行前分别核验。

## 三个项目如何配合

| 仓库 | 对应指南 |
| --- | --- |
| BH2017-LOCRENASCI | [Android 构建与游玩](https://github.com/ClearwSea/BH2017-LOCRENASCI/blob/main/docs/agent-guide.md) |
| BH-LOCRENASCI-ACT-EDIT | [存档生成、编辑与导回](https://github.com/ClearwSea/BH-LOCRENASCI-ACT-EDIT/blob/main/docs/agent-guide.md) |
| BH-NX-Ferculum | [NS 构建、接入与验证](agent-guide.md) |

按用户当前目标选择指南，不要求三个仓库全部安装。需要存档时转到编辑器，保存后按目标平台返回游戏指南；已有适用材料不重复索要。跳转时保留用户已提供的平台、版本、文件路径、目标及授权信息，不从头重复提问。跨仓库指南不能代替实际版本与接口兼容核验。

## 开始前：沟通方式与网络自测

### 用用户听得懂的话推进

根据用户的提问和反馈判断其熟悉程度，不给用户贴“技术水平低”的标签。用户不熟悉构建、签名、文件路径等概念时，必须在**每一步**用简短、通俗的话说明：现在要做什么、为什么要做、当前有什么阻碍、下一步需要用户做什么，以及该步骤实际需要注意的事项。没有阻碍或不需要用户操作时直接说清楚，不制造问题，也不要堆砌整篇技术文档。

在即将进入依赖用户选择或材料的步骤前，及时预提问，不要等到操作失败才问。首次先问设备和已有材料，下载前说明文件大小与存放位置，安装前确认新装还是覆盖更新及存档备份，生成存档前解释新号与体验号区别。一次只问当前及紧接着下一步必需的少量问题；已明确的答案和授权直接复用，能自行检查的先检查。等待回答时继续不依赖该答案的工作。

例如：“我先检查下载速度，因为构建工具包比较大，避免下载到一半才发现网络不通。你现在不用操作；接下来请告诉我电脑还有多少可用空间，或者允许我直接检查。”网络受阻时则说明具体影响和替代方案，而不是只抛出报错或说“请自行解决”。

### 先测试实际下载环境，不假定有可靠代理

默认用户的 Agent **没有可靠的网络代理**。在下载大文件之前，先用可用工具做一次有超时、有流量上限的快速检查。即使已能阅读仓库，也不能据此认定 Release 大附件能顺畅下载；Agent 云端的网络结果也不代表用户电脑的网络。

1. 在实际负责下载文件的环境中，检查仓库/Release 页面或 GitHub API 的连通性，记录响应状态和耗时。单次连接超时可设约 10 秒、请求总时限约 20 秒；失败时有限重试，不无休止等待。
2. 从实际 Release 元数据中取得一个真实附件 URL，跟随重定向测试文件下载。优先读取约 1–4 MiB 的小段数据，设置约 20 秒总时限和硬性读取量上限，记录实际字节数、平均速度及是否中断。服务器忽略 Range 时也要主动停止，不能把测速变成整包下载。仅做 HEAD 请求不能证明下载速度。
3. 结合实际附件大小估算下载耗时。例如，连续超时、连接反复中断，或大包测速持续低于约 200 KiB/s，应视为质量不佳的信号；这些是判断参考，不是固定通过门槛。只有鉴权失败、API 限流或链接失效时，应区分错误原因，不能全部归因于网络。
4. 不能执行网络请求时，如实说明未能自测；提供用户可以执行的短步骤或命令，并解释需要返回什么结果，不虚构测试成功。

如果连接质量不佳，**必须明确告诉用户“当前环境到 GitHub 的网络连接质量较差”**，附上实际测得的超时、速度或预计等待时间。这可能发生在中国大陆部分网络下，但不能仅凭用户所在地区推断。同步说明构建仍缺哪些下载、其他准备工作能否继续。

随后由你主动制定并尝试可行的 Release 获取方案，不能只让用户“自己找代理”。根据自身工具和用户环境自行选择：可用的官方附件直链、支持续传的下载工具、用户已有且允许使用的代理/其他网络、浏览器下载后交给 Agent，或在获准且网络可用的另一环境下载后传回。需要用户配合时，用人话说明该方案、为什么可行、用户只需完成哪一步。

本文不指定永久有效的加速站或代理地址。第三方中转若确有需要，应先核实可用性并告知用户来源；不向其发送 GitHub 凭据、签名密钥或存档，不关闭 TLS 证书校验。取得文件后对照官方提供的 SHA-256 校验；暂时无法取得官方校验值时明确标记未验证，不宣称文件可信。代理安装、系统设置变更和付费服务按用户授权处理，不以解决下载为由擅自更改环境。

## 1. 先说明实际交付边界

本仓库是指定 Android ARMv7 客户端的 Nintendo Switch 兼容层与接入参考，包含运行时、补丁、输入/音视频桥接、Python 托管和封装工具。它**不是附带完整游戏与后端的一键安装包**。用户提供仓库地址后，先判断其想研究源码、构建兼容层，还是最终在 NS 游玩。

阅读 [README](../README.md)、[AGENTS.md](../AGENTS.md)、[构建说明](build.md)、[开发接入指南](developer-guide.md)、[接入说明](integration.md)及 [依赖锁定文件](../manifests/dependencies.lock.json)。读取所选 Release 的实际附件清单，不假定其中存在完整 NSP、离线工具套件或可直接运行的游戏。

用户想玩时，用人话说明：“这个仓库提供让游戏在 Switch 上运行的适配代码。还需要匹配的游戏文件、资源和实际服务端。我会先检查已有材料，列出缺什么；只有兼容层或演示后端编译成功，还不能开始玩。”缺少后端接入属于实际开发工作，不承诺解压即玩，也不把它隐瞒为普通安装步骤。

## 2. 检查环境并索要材料

先检查已有文件，再分步询问：电脑系统及允许使用的构建目录、Linux/WSL/容器环境、可用空间；NS 型号和当前系统/运行环境；已有可运行版本还是首次部署；能否通过已有工具访问 SD 卡；是否有需要保留的进度。README 中的实测环境只是参考，不代表任意系统或设备均兼容。

构建需要支持 `linux/amd64` 的 Docker 环境及 ARM32 交叉依赖。Windows、WSL、容器的路径需明确映射，不沿用其他电脑路径。运行环境必须已具备执行该项目产物的条件；若不具备，明确说明缺失条件，不把修改设备环境混入普通部署。部署和存档写入按用户授权进行。

**明确向用户索要以下外部材料，并给出实际文件夹或可访问的交付方式：**

| 材料 | 需要核对什么 |
| --- | --- |
| 匹配的 1.9 Android ARMv7 APK、原生库及资源 | 版本名称不足以证明匹配；核对 README 中目标 `libil2cpp.so` SHA-256、补丁前像和客户端配置 |
| 游戏资源载荷 | 必须匹配客户端读取路径及网络/资源桥接配置，不直接把任意 APK 改名当成可用资源 |
| 实际 Python 游戏业务后端及运行数据 | 需要适配 `nx_backend.configure`；HTTP 示例不是游戏后端 |
| 兼容 `account.json` | 新玩家可由配套编辑器生成；旧玩家使用实际游玩档，并先备份 |
| 字体 | 支持启动界面所需文字的字体及其许可，用于生成字库 |
| 完整 NSP 封装所需材料（选择此路线时） | 控制文件、图标、匹配工具和用户已有的外部密钥文件；不要将密钥写入仓库或回显 |

用户不清楚客户端和资源从哪里获取时，提供 [Android README 的参考入口](https://github.com/ClearwSea/BH2017-LOCRENASCI#客户端参考信息)，主动展示对应 B 站链接：

| 参考材料 | 视频 |
| --- | --- |
| 1.9 客户端 | [BV1UU4u6yE8R](https://www.bilibili.com/video/BV1UU4u6yE8R/) |
| 2.4 参考客户端（演示版） | [BV1zJ411N7Qz](https://www.bilibili.com/video/BV1zJ411N7Qz/) |
| 7.x PC Windows 资源（7.0 参考） | [BV1xCpPezEeM](https://www.bilibili.com/video/BV1xCpPezEeM/) |

先对照当前 README 检查入口是否更新，明确这是参考视频，不是已验证的下载直链；要求按视频说明获取客户端/资源，而非下载视频本身。说明匹配哈希仍需检查，不承诺参考入口中的任意文件能直接用于 NS。

**1.9 客户端是本仓库目标输入；2.4 和 7.x 不是编译最小示例的必要条件。** 只有选择 Android 项目的数据/资源生成路线且该路线要求时，才明确向用户索要 2.4、7.x 及其补充包，并转到 [Android 构建指南](https://github.com/ClearwSea/BH2017-LOCRENASCI/blob/main/docs/agent-guide.md)。已有匹配资源和后端时，不要求重复下载全部输入。

向新手给出具体存放位置，接受现有路径、上传、可访问共享位置或下载链接；Agent 负责检查压缩包、提取所需文件和定位资源。没有网络或材料时说明当前能继续做哪些准备，不让用户自行猜格式。空间按实际客户端、资源、依赖、容器镜像和输出体积估算，本仓库未给出统一完整构建空间保证，勿直接套用 Android 的建议。

## 3. 准备锁定工具链和最小示例

先按网络自测检查实际访问 GitHub、GHCR 和其他依赖来源的环境；一个站点可用不代表全部可用。依赖版本、源码提交和容器 digest 从 [锁定文件](../manifests/dependencies.lock.json)读取，额外 Python/NumPy 归档见 [依赖表](../poc/python32_probe/dependencies.json)。不使用记忆中的“最新版”替换锁定依赖。

按 [开发接入指南](developer-guide.md)准备以下布局，将示例占位符替换为已确认的真实路径：

```text
<build-root>/
  BH-NX-Ferculum/
  libnx32/prefix/
  mesa32/prefix/
  ffmpeg32-src/prefix/
  python32-probe/
```

按锁定提交构建 libnx32、Mesa，并使用匹配工具链。镜像需提前获取，主构建脚本使用 `--pull=never`，不会自动拉取缺失镜像。只准备容器镜像不等于 ARM32 依赖前缀已生成。

依赖具备后，在适合的交叉编译环境内执行最小示例；先将 `BH3_BUILD_ROOT` 设为真实构建根目录：

```sh
LIBNX32="$BH3_BUILD_ROOT/libnx32/prefix" \
PORTLIBS="$BH3_BUILD_ROOT/mesa32/prefix" \
RUNTIME_DIR="$BH3_BUILD_ROOT/BH-NX-Ferculum/third_party/android32" \
sh "$BH3_BUILD_ROOT/BH-NX-Ferculum/examples/minimal-app/build.sh"
```

最小示例不加载游戏或服务端，只验证 AArch32 与图形环境。按其实际产物和用户已有运行方式部署，确认颜色画面和日志；没有实机权限时标记“示例已构建，设备验证待完成”。这一阶段不宣称游戏可玩。

## 4. 准备完整运行依赖与实际后端

按开发指南构建 ARM32 FFmpeg、CPython 和 NumPy。FFmpeg 需要目标 ARM softfp ABI、四字节枚举及所需解码组件；不能链接宿主机库。按脚本准备 Python 标准库、NumPy Python 模块和业务依赖，主程序编译前确认生成的 `numpy_inittab.h`、`objects.rsp` 等实际存在。

准备字体，在仓库根目录按 [字库步骤](developer-guide.md#启动文字字库)生成 `boot_ui_font.h`。先确认 Pillow 可用；需要安装依赖时说明并按授权处理。字体由用户提供，保留其许可，生成字库不提交到公共源码。

```sh
python3 tools/generate_boot_ui_font.py --font /actual/path/to/font.ttf
```

上面的字体路径只是命令示例，执行前由 Agent 替换为现场路径。

业务后端按 [托管接口](../references/bh3-1.9/python-server-host/README.md)实现 `nx_backend.configure(runtime, data)`，返回：游戏会话处理器、HTTP 处理器、已初始化校验文件数。验证 `_nxipc` 提供的 socket 方法、数据初始化、持久化和 ready 通知顺序。

`nx_backend.example.py` 只演示 HTTP 并返回 501，不实现游戏协议，不能直接改名后声称已接入。可以研究 [Android 服务端源码](https://github.com/ClearwSea/BH2017-LOCRENASCI)，但需要确认所选提交与数据兼容，并适配 `server19_portable.configure`、`server19.GameHandler`、`server19_http.make_handler` 等入口。Android 后端 `start` 的 TCP/HTTP 监听流程不能直接替代 NS 的内存通道托管。没有现成后端时，说明适配工作范围并取得用户对该开发任务的明确意图。

默认 standalone 目录示意如下，实际以 `port_config.h`、路径桥接及后端配置为准：

```text
/switch/bh3_2017/
  game.apk
  libmain.so / libulua.so / libunity.so / libil2cpp.so
  libAkSoundEngine.so / libAVProLocal.so
  python/stdlib/
  python/runtime/
  server-stage3/nx_boot_profile.py
  server-stage3/nx_server.py
  server-stage3/nx_backend.py
  server-stage3/data/
```

特别检查模板残留的路径：本文编写时 `nx_boot_profile.py` 的 `PYTHON_ROOT` 指向探针目录，而非上述 standalone Python 目录。接入时统一实际路径，不直接照搬模板常量。还需核对客户端返回的地址/端口与桥接配置。

## 5. 资源清单与主程序

将已准备的资源、业务和必要初始化数据组织到外部 `<romfs>/payload/`。明确哪些文件是仅在不存在时初始化的 S 类数据；初始 `account.json` 若随包提供，应按种子文件处理，不能把用户现有存档打包成会覆盖的普通资源。

在仓库根目录使用以下命令示例，先替换所有占位符：

```sh
python3 tools/resources/prepare_manifest.py --romfs <romfs> --header <new-header> --seed server-stage3/data/account.json
```

不提供初始存档则省略 `--seed`。`manifest.tsv`、`package.id` 和新头文件必须尚不存在，头文件放在 payload 外。将生成头文件用于 `poc/m3_boot/source/package_identity.h`，再编译；载荷改变后同步重新生成三者，不能继续沿用旧资源身份。

全部依赖、字库、后端和资源身份准备好后，在仓库根目录运行：

```sh
bash tools/build/release-original.sh
```

`BH3_BUILD_ROOT` 必须已设置。按实际脚本检查日志及产物；本文编写时输出到 `release-reference/`，其中 `release.nsp` 是 **ExeFS 容器，不是完整可安装游戏包**。代码编译通过也不代表业务和资源接入通过。

## 6. 完整封装与部署

需要完整安装包时，按 [开发接入指南](developer-guide.md#完整安装包与卡带入口)准备 `digital-local.nsp`、`digital-package/control/` 内的 `control.nacp` 与图标，以及 `hacBrewPack`、`hactool` 和外部密钥。密钥来自用户已有获准环境，不代找、不公开。工具固定来源见锁定文件；封装脚本需要的 Python 版本以当前指南为准。

以下为命令模板，由 Agent 在检查脚本及实际文件后填写：

```sh
python3 tools/packaging/build_standalone_nsp.py --root <package-root> --keys <external-keys> --title-id <matching-title-id> --prepared-romfs <romfs>
```

主程序 NPDM 与封装 title ID 必须一致，控制文件名称及身份必须满足脚本检查。当前构建脚本含固定程序 ID；若修改，需同步相关配置，不能只改打包参数。需要卡带入口时再研究对应工具，不把此步骤当成每位用户的必需操作。

确认封装校验结果、产物大小与 SHA-256，记录所用提交、依赖和资源身份。部署前确认目标设备、实际 SD 路径及原档备份。通过用户已有且获准的安装/传输方式操作，不能把 ExeFS 容器当完整 NSP 安装，也不因一次安装失败就删除全部资源。

## 7. 存档、实机验证与回退

需要新号或体验号时，使用 [存档编辑器指南](https://github.com/ClearwSea/BH-LOCRENASCI-ACT-EDIT/blob/main/docs/agent-guide.md)。本文编写时 NS 新号“首次登录”完整开场未验收，应保持关闭；仅当当前文档明确更新验收结论时调整。

编辑或替换前 HOME → X 完全关闭游戏；默认存档是 SD 文件 `sd:/switch/bh3_2017/server-stage3/data/account.json`，不是系统存档槽。DBI/MTP、FTP 或直连 SD 卡只按实际环境使用，先复制到电脑并独立备份，再编辑、放回原路径。自定义后端的路径和存档格式必须另行核对。

逐步验证：启动与资源身份校验、本地后端 ready、进入舰桥、输入及音视频、普通关卡进入/退出/结算、重启保留进度。一次只排查当前失败阶段，不把颜色示例、HTTP 演示或离线封装校验写成完整实玩通过。

失败时保存运行日志、具体画面、构建身份和复现步骤；先检查 ABI/库指纹、运行路径、标准库与业务模块、后端接口、资源清单及存档兼容。回退到保留的程序/资源组合，并在完全退出后恢复相配的存档；不要承诺任意新旧存档兼容。真实存档和日志外发需用户同意，排除个人信息与密钥。

## 8. 最终交付应说明什么

列出已完成的阶段和仍缺的接入项、提交及构建配置、产物和校验值、部署方式、实际数据路径、备份/回退位置、已实测设备与未验证范围。若仅完成源码/兼容层编译，明确如此说明；用户目标是游玩时，继续处理获准范围内的依赖和接入，无法继续则具体说明缺少何种材料或决策。
