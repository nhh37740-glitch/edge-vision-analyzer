# Edge Vision Analyzer 架构

状态：有效设计契约；v0.1 核心框架、采集模块与控制面已实现，v0.2 的帧差漏斗 MotionNode（64×48 灰度 MAD）与 NcnnDetectorNode（NCNN 目标检测与 NMS）模块已实现，v0.3 的 JPEG／Base64 编码、`GenAiWorker` 异步投递与 `EventTrackerNode` 事件聚合已实现，以上模块已由 `src/main.cpp` 接入端到端运行入口；PC 存储服务已实现，部署脚本与模板已就绪，真机验收待执行。维护：项目接力 Agent；日期：2026-11-24（北京时间）。PATCH-003 覆盖存储与历史查询，PATCH-005 规定模块链接，PATCH-007 补充局域网、私有配置、资源生命周期与服务守护，PATCH-008 规定内存日志采集及 PC 日志落盘／查询，PATCH-009 补充部署对时与温度观测。本文与 [roadmap](roadmap.md)、[api](api.md) / [OpenAPI](openapi.yaml)、[AGENTS](../AGENTS.md) 共同构成仓库自包含依据。

## 1. 物理拓扑与三个服务入口

```text
[手机 IP Webcam] ──(Type-C 转网口扩展坞)──┐
                                         │
[魔百盒 Armbian 2GB] ────(网线)───────────┼──▶ [以太网交换机] ──▶ (可选：上级路由器 DHCP)
  边缘控制与实时输出 :8081                │
[联想奔腾 PC 16GB] ──────(网线)───────────┘
  交叉编译 + llama-server :8080 + cloud-storage :8090
```

所有设备接入同一有线局域网；手机与魔百盒不使用 USB 直连。手机扩展坞若有 PD 供电口，可独立供电并持续推流。实际延迟、稳定性与供电能力待现场验收。

运行期只访问这三台设备在纯交换机 LAN 内的服务，不依赖公网或云厂商 API。可选路由器仅承担 DHCP 分配；手机、边缘和 PC 的地址／端口由本地配置给出，文档中的开源仓库链接不是运行期访问目标。

- 交换机接上级路由器：三台设备通过 DHCP 获取同网段地址，按租约更新配置中的手机和 PC 地址。
- 独立离线交换机：手动设置同网段静态 IP；纯交换机不提供 DHCP。示例联想 `192.168.10.10`、魔百盒 `192.168.10.11`、手机 `192.168.10.12`，掩码 `255.255.255.0`。离线局域网通信不需要网关和 DNS。
- 手机视频示例 `http://192.168.10.12:8080/video`；LLM `http://192.168.10.10:8080`；PC 存储 `http://192.168.10.10:8090`；边缘控制 `http://192.168.10.11:8081`。手机实际流路径和以太网设置能力需真机确认。
- PC 的 `llama-server :8080` 与 `cloud-storage :8090` 是两个独立进程，各有 HTTP 入口；不把存储路由挂在 LLM 进程中。

## 2. 机器职责与资源预算

| 设备／进程 | 职责与约束 |
| --- | --- |
| 联想 PC | 2021 年奔腾 6 系列，x86_64 Linux，16GB DDR3；开发、本机离线验证和 aarch64 交叉编译 |
| PC `llama-server :8080` | 多模态推理，返回中文场景描述；不负责事件图片持久化和历史查询 |
| PC `cloud-storage :8090` | 独立 FastAPI + SQLite 模拟云存储，接收干净 JPEG 和结构化结果；保存图片、写库并提供历史查询 |
| 魔百盒 | Amlogic ARMv8 四核 Cortex-A53，Armbian，2GB RAM；无状态边缘计算，仅实时采集、帧差、检测、瞬时事件聚合、异步投递与流输出；禁止编译大型 C++ 依赖 |
| 手机 | IP Webcam 提供 HTTP MJPEG 或 RTSP，经有线扩展坞接交换机 |

魔百盒常驻 RSS 必须 **<150MB**。所有节点输入队列有界，默认容量 **2**，满时丢弃最旧帧；Nous 待处理队列容量 **1**，只保留最新任务。只保存当前活跃事件的瞬时 ID、时间、最佳目标等聚合状态，不留本地历史事件表、历史图片缓存或失败任务磁盘队列；结束、丢弃或完成后释放状态／帧引用，不重试积压。

检测线程 **2**；默认解码 **640×480 RGB24 / 15 FPS**，检测输入 **320×320**，帧差灰度 **64×48**。这些是设计目标与约束，并非当前实测结果。

无状态运行仍保留阶段 A 中用户显式要求的 `.jpg` 截帧和 15～30 秒仅视频 remux 录片，用于形成阶段 B 素材；录片输出只保留视频轨，不创建音频等其余输出流。它们不是常态事件留存或失败补偿缓存。本轮不实际录制或连接设备。

## 3. 软件数据流

```text
魔百盒：手机有线流 / 本地 demo_clip.mp4（循环）
      │ 单次拉流；阶段 A 显式 remux 原始视频 AVPacket 录片（仅视频轨）
      ▼
CaptureNode（FFmpeg → sws_scale → 640×480 RGB24）
      │ shared_ptr<FrameMeta>；节点队列默认 2，满则丢旧
      ▼
MotionNode（64×48 MAD；静态帧透传，跳过检测）
      ▼
NcnnDetectorNode（有运动时 NCNN + NMS）
      ▼
EventTrackerNode（仅当前活跃事件的瞬时聚合状态）
      ├──事件触发──▶ GenAiWorker / Nous（单后台线程；队列 1）
      │                │ 干净帧 JPEG 质量 70，仅编码一次 → Base64
      │                ├─1. llm.enabled 时 → PC llama-server :8080
      │                │                    成功取描述，失败描述为空
      │                └─2. storage.enabled 时 → PC cloud-storage :8090
      │                     同一 JPEG Base64 + 元数据 + 可选描述
      ▼
StreamOutNode（独立 OSD 输出画面；按需 MJPEG；mode=rtsp 时 RGB24 推 MediaMTX）
      ▼
ApiServer :8081（实时控制、指标、快照、流；不提供历史查询）

联想 PC cloud-storage :8090
      ├── storage/images/YYYY-MM-DD/<event_id>.jpg
      ├── storage/events.db（SQLite 结构化事件）
      └── 历史分页列表 / 干净抓拍图 HTTP 查询
```

所有 C++ 帧处理阶段遵循 `BaseNode` + `attach_next()`，节点各用单工作线程；不在 main 中拼接散装线程和多重队列。`FrameMetaPtr` 共享采集原始 RGB，下游不改写原缓冲。OSD 按需生成独立输出画面；Nous 使用未画框的干净帧，帧引用生命周期受有界队列和单个在处理任务约束。`shared_ptr` 只负责生命周期，不替代线程同步。完整 `FrameMeta` / `TargetBox` / `BaseNode` 设计见 [roadmap](roadmap.md)。

Nous 为单后台工作线程，顺序执行可选 LLM 和可选 Storage：JPEG 以质量 **70** 在内存编码一次，Base64 在两个 HTTP 请求间复用；不把 OSD 图送入分析或保存。LLM 连接／读取超时默认 **3 / 25 秒**；失败将 `llm_summary` 置为 `""`，继续执行已开启的存储步骤。存储请求默认超时 **5 秒**，失败只记必要日志并释放当前任务；不重试、不补建磁盘队列、不阻塞视频线程。关闭 LLM 仍可保存原图与目标元数据；关闭 Storage 不影响推理和实时输出；两个开关都关闭时跳过 Nous 编码／HTTP 工作。

每个事件任务完成全部已开启的投递步骤后，无论成功、失败、超时或提前退出，立即释放任务持有的 RGB／JPEG／Base64 图像缓冲、请求体及帧引用；队列丢旧同样释放旧任务。LLM 与 Storage 之间仅为复用同一 JPEG 保留必要数据，不在任务结束后沿用上一张图。不能仅调用保留大容量的 `clear()` 就声称已释放图像内存；由 RAII／作用域退出结束任务所有权，不增加异步清理线程或外部清理脚本。

事件无目标宽限默认 **5 秒**，触发冷却默认 **15 秒**；连续目标只更新当前活跃状态，不逐帧投递。PC 接收的是触发时代表帧与结构化记录，不要求魔百盒留存历史或等待 PC 反向回填；历史列表以 PC 已成功接收的数据为准，不能宣称投递必达。

## 4. PC 云存储边界

`cloud-storage/` 与魔百盒 C++ 交叉编译完全解耦。本轮仅落盘 [README](../cloud-storage/README.md)、[requirements.txt](../cloud-storage/requirements.txt)、[schema.sql](../cloud-storage/schema.sql) 及空服务占位；FastAPI `server.py` 实现在 `v0.3-nous` 后续开发完成。运行时 `cloud-storage/storage/` 整体忽略，不将图片、SQLite 数据库及其运行文件入库。

- 未来 `POST /api/v1/events` 解码 `image_base64`，把原始干净 JPEG 写至 `cloud-storage/storage/images/YYYY-MM-DD/<event_id>.jpg`，结构化元数据和相对路径写入 `cloud-storage/storage/events.db`。
- SQLite 字段与索引正式依据为 `schema.sql`：`event_id` 主键，帧号、时间、最高置信度目标、目标 JSON、可空描述、LLM 耗时、图片相对路径和创建时间；不保存边缘历史队列。
- 历史 `GET /api/v1/events?limit=20&offset=0` 与 `GET /api/v1/images/{date}/{filename}` 仅由 PC `:8090` 提供；日期目录、排序、分页、URL、错误及重复 ID 的语义统一见 [api](api.md) / [OpenAPI](openapi.yaml)。边缘 `:8081` 不代理历史接口。
- `GET /health` 反映云存储数据库连通状态；边缘 `/api/v1/health` 分别反映 LLM 与 Storage 可达性，不把 LLM 存活等同于存储可用。
- PATCH-008 的日志接收／查询也归 PC `:8090`，与事件图片和 SQLite 的职责分开；日志按第 10 节追加文件，不写事件表，不把采集器塞入视频处理线程。

## 5. 开源参考与本项目约束

| 技术范围 | 开源基线 | 采用的模式／本项目契约 |
| --- | --- | --- |
| 边缘／存储分离 | [Kerberos Agent](https://github.com/kerberos-io/agent)、[Kerberos Vault](https://github.com/kerberos-io/vault) | 参考边缘处理与独立集中存储的职责分离；本项目自定无状态边缘、FastAPI／SQLite 与 8090 API，不声称照搬上游实现或上游本身禁止本地录制 |
| core | [VideoPipe](https://github.com/sherlockchou86/VideoPipe) | 参考节点连接和元数据流转；本项目 BaseNode、容量 2 丢旧策略为内部契约 |
| detector / transport | [Frigate](https://github.com/blakeblackshear/frigate) | 帧差 → 检测 → 瞬时事件聚合 → 异步 GenAI；历史持久化迁到 PC |
| capture | [FFmpeg demux_decode.c](https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/demux_decode.c) | 原生 C API、sws_scale、RTSP TCP、I/O 超时，不引入 OpenCV |
| detector | [NCNN](https://github.com/Tencent/ncnn)、[NanoDet](https://github.com/RangiLyu/nanodet/blob/main/demo_ncnn/nanodet.cpp)、[YOLO-FastestV2](https://github.com/dog-qiuqiu/Yolo-FastestV2) | ncnn::Net、FP16／NEON、2 线程、from_pixels_resize 与 NMS |
| transport | [llama-server](https://github.com/ggml-org/llama.cpp/tree/master/tools/server)、[cpp-httplib](https://github.com/yhirose/cpp-httplib)、[stb](https://github.com/nothings/stb) | 原生健康／多模态接口、内存 JPEG、顺序可选 HTTP 投递 |
| streamer / api | [cpp-httplib](https://github.com/yhirose/cpp-httplib)、[MediaMTX](https://github.com/bluenviron/mediamtx) | 实时 REST／MJPEG；RTSP 通过 FFmpeg 管道推外部服务 |
| common / tests | [nlohmann/json](https://github.com/nlohmann/json)、[GoogleTest](https://github.com/google/googletest) | 统一 JSON，少量纯离线 GoogleTest + CTest |

当前未引入或复制上游实现；依赖版本和许可随实现登记。FFmpeg 版本冻结时按[官方协议文档](https://ffmpeg.org/ffmpeg-protocols.html)核对 `rtsp_transport=tcp`、`rw_timeout`，及旧 `stimeout` 与所选版本 `timeout` 的适用性。LLM 请求沿用[原生服务契约](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)，不修改上游进程。

## 6. 目录、业务归属与数据归属

本次只改设计和骨架，不创建业务实现。业务边界按运行职责拆为两项；下列技术目录各有一个责任范围，不把 HTTP 依赖当作模块归属或跨业务源码复用：

| 业务模块 | 专属技术范围 | 数据归属与功能结果 |
| --- | --- | --- |
| 边缘实时视觉 | `src/{core,common,capture,detector,transport,streamer,api}/` | 拥有实时帧、检测和活跃事件瞬时状态，提供控制与实时输出；通过契约投递 PC，不维护历史数据 |
| PC 事件存储 | `cloud-storage/` | 独占图片、SQLite、日志文件及相应历史查询；不导入／借用边缘 C++ 一级模块，不参与 ARM64 构建 |

后续每次修改仍须按 AGENTS 给出“业务模块 → 修改／新增的专属一级模块 → 用户功能结果”、文件范围和验收清单；框架、配置等技术目录的实现拆分须符合该独占规则，不以本表默认批准额外代码修改。`llama-server` 是独立外部服务依赖，不归属任何边缘一级模块。

## 7. 阶段验收与本轮交付边界

阶段 A：手机有线流 → 用户显式截帧 JPEG → 15～30 秒仅视频原包 remux 录片 → 切换本地循环。阶段 B：本地循环素材完成后续无人值守开发。详细文件交付和有限离线测试规划集中在 [roadmap](roadmap.md)。

| 里程碑／后续发版 Tag | 责任目标 |
| --- | --- |
| `v0.1-oculus`（神目） | 采集录片与基础控制面 |
| `v0.2-discern`（洞察） | 帧差与 NCNN 检测 |
| `v0.3-nous`（神思） | 瞬时事件聚合、可选推理／存储投递、PC 存储服务实现与历史查询 |
| `v1.0-vox`（神谕之声） | OSD、视频输出和 systemd 交付 |

实时 MJPEG 仍只在观看或快照请求时编码，无请求时不编码；可选 RTSP 使用 `-preset ultrafast -tune zerolatency -b:v 500k`。Nous 的事件 JPEG 编码与实时 OSD 输出编码用途不同，不把两者混用。`src/main.cpp` 按 `config.streamer.mode` 装配输出：`mjpeg` 模式不创建推流子进程，保持按需 JPEG；`rtsp` 模式把可选 `RtspPusher` 注入 `StreamOutNode`，节点逐帧在独立缓冲生成 OSD，以 RGB24 投递有界写队列，共享采集帧保持只读。推流器启动失败（例如找不到 `ffmpeg`）只打印告警，流水线与 MJPEG 端点继续可用，不因此退出进程；停止时先停节点再回收推流进程，避免节点线程卡在编码器上。RTSP 地址、宽高与帧率分别取自 `config.streamer.rtsp_url` 和 `config.input`，真机 MediaMTX 联动验收待执行。后续验收需包含关闭上游／超时降级、队列丢旧与释放、优雅退出及 RSS **<150MB**；指标以实测记录为准。

初始化和核心框架已交付：`edge_core` 的节点、队列与源分类，以及 `edge_common` 的配置和指标已实现并有本地离线测试。`edge_capture` 的 FFmpeg 拉流、RGB24 解码、EOF 循环、断线重连、截帧和仅视频 remux 录片，以及 `edge_api` 的实时控制端点和运行入口也已实现；`test_capture` 覆盖本地解码、循环与非法源，`test_api` 用伪 `SourceControl` 覆盖健康探针与切源。`edge_detector` 的 MotionNode／NcnnDetectorNode、`edge_streamer` 的 OSD／RTSP 与 `edge_transport` 的 JPEG／Base64、`GenAiWorker` 及 `EventTrackerNode` 已实现并由 `src/main.cpp` 装配为 CaptureNode → MotionNode → NcnnDetectorNode → EventTrackerNode → StreamOutNode 的端到端流水线，API 已接入快照与 MJPEG 回调，推流器已按 `config.streamer` 装配为 MJPEG／RTSP 双模并接入优雅退出；PC 存储服务已实现，部署脚本与模板已就绪，真机与 MediaMTX 验收待执行。后续变更继续通过受保护 `main` 的 PR／Squash 流程，不关闭保护或直接推送 main；静态检查和工作流定义不等于业务构建或运行验收。

## 8. 模块构建与链接边界（PATCH-005）

根 [CMakeLists.txt](../CMakeLists.txt) 已启用 CXX 17；Issue #1 建立了 `edge_core` 静态库、`test_build` 和 `cmake/aarch64-toolchain.cmake`。已落地的源模块按上表创建对应 `STATIC` 库；其余模块获准落地后按同样边界加入，不把所有 `.cpp` 填进主程序。

| 项目静态库 | 直接依赖 |
| --- | --- |
| `edge_core` | 标准库、`Threads::Threads` |
| `edge_common` | `nlohmann_json::nlohmann_json` |
| `edge_capture` | `edge_core`、`edge_common`、FFmpeg、stb |
| `edge_detector` | `edge_core`、`edge_common`、NCNN（FetchContent 固定 Release Tag `20261020`） |
| `edge_transport` | `edge_core`、`edge_common`、cpp-httplib、stb |
| `edge_streamer` | `edge_core`、`edge_common`、stb |
| `edge_api` | `edge_core`、`edge_common`、cpp-httplib |

`src/main.cpp` 已作为唯一运行入口，当前链接 `edge_core`、`edge_common`、`edge_capture`、`edge_detector`、`edge_streamer`、`edge_transport` 和 `edge_api` 形成边缘可执行文件 `edge-vision-analyzer`，按配置串联 CaptureNode → MotionNode → NcnnDetectorNode → EventTrackerNode → StreamOutNode，并注册 `SIGINT`／`SIGTERM` 优雅退出；`edge_detector` 的 MotionNode／NcnnDetectorNode、`edge_streamer` 的 OSD／RTSP 推流与 `edge_transport` 的 `jpeg_base64`／`GenAiWorker`／`EventTrackerNode` 均已实现并有离线测试。FFmpeg 等非头文件第三方底层运行库保持动态链接，不将“项目模块 STATIC”理解为全静态程序。依赖以目标为范围声明，实际 imported target 名称随所选版本登记，不把 FFmpeg 全局注入 core/common 或所有模块。`cloud-storage` 仍为独立 PC 进程，不参与此 C++ 链接图。

`include(CTest)` 已启用；每个单测可执行文件仅直接链接 `edge_core`、`edge_common`、被测模块和 `GTest::gtest_main`；被测模块按上表带入自身必要依赖。`test_detector` 不链接 capture、主程序或 FFmpeg。交叉编译设置 `BUILD_TESTING=OFF`；`cmake/aarch64-toolchain.cmake` 已由 Issue #1 建立，工具链与依赖版本记录在 README。

`CMakeLists.txt` 已为 `edge-vision-analyzer` 添加 `install(TARGETS edge-vision-analyzer RUNTIME DESTINATION bin)`，为 release 打包提供安装目录；本机已可构建该目标，不等于已交叉编译、打包或在目标机部署。

## 9. 配置、FFmpeg 生命周期与服务守护（PATCH-007）

仓库只维护公开的 [config.example.json](../config/config.example.json) 模板。部署时先复制为 `config/config.local.json`，再填写实际手机／PC 地址、端口和必要凭据；本地文件受 Git 忽略，不把现场私有配置写回公开模板。边缘进程使用本地配置启动，`GET /api/v1/config` 仍按接口脱敏；配置文件分离不改变 REST 契约。

FFmpeg 原生拥有型资源统一用 `std::unique_ptr<T, 自定义 deleter>` 管理，五类对象的释放方式与作用域如下。借用型指针不误纳入拥有型智能指针，异常、切源、重连和停止路径都由同一 RAII 所有权收尾。

| 拥有型资源 | 自定义 deleter／释放语义 | 生命周期 |
| --- | --- | --- |
| `AVFormatContext` | 输入使用 [`avformat_close_input`](https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html)；显式 remux 的输出上下文另按输出 API 关闭 I/O 并 `avformat_free_context`，不可套用输入关闭器 | 输入源／录片会话结束、切换或重连时释放，不逐帧销毁 |
| `AVCodecContext` | [`avcodec_free_context`](https://ffmpeg.org/doxygen/trunk/group__lavc__core.html) | 解码器会话结束／替换时释放 |
| `AVFrame` | [`av_frame_free`](https://ffmpeg.org/doxygen/trunk/group__lavu__frame.html) | 对象可在解码循环复用，每帧转换／使用结束及时 `av_frame_unref`，离开对象作用域再 free |
| `AVPacket` | [`av_packet_free`](https://ffmpeg.org/doxygen/trunk/group__lavc__packet.html) | 对象可在读取循环复用，每包解码／remux 使用结束及时 `av_packet_unref`，离开对象作用域再 free |
| `SwsContext` | [`sws_freeContext`](https://ffmpeg.org/doxygen/trunk/group__libsws.html) | 缩放器会话结束或尺寸／格式改变时替换并释放 |

`av_frame_unref`／`av_packet_unref` 释放当前引用的数据缓冲，不替代对象最终 free；也不能在其他处理步骤仍需该数据时提前 unref。其他三类管理对象按自身会话生命周期释放，不把“处理后立即释放”误写为每帧重建所有上下文。RGB `FrameMeta` 与任务图像的独立所有权、节点有界队列仍遵守前文约束。

应用日志只写 stdout／stderr，不在魔百盒创建业务日志文件，不加入定时删除日志、帧或缓存的外部清理脚本。服务模板 [edge-vision.service](../deploy/edge-vision.service) 将标准输出与错误交给 journal，内存存储与独立采集器按第 10 节部署契约接力；模板未安装，当前不声称目标机已运行或日志已接管。

该模板采用工作目录 `/opt/edge-vision-analyzer`，启动命令为 `bin/edge-vision-analyzer --config /opt/edge-vision-analyzer/config/config.local.json`；`src/main.cpp` 已实现 `--config`，并支持阶段 A 的 `--record-clip` 与 `--duration`，启动时按配置装配采集、帧差、检测、事件聚合、异步投递与流输出节点，注册 `SIGINT`／`SIGTERM` 后按上游到下游顺序停止。模板尚未安装到目标机；安装文件并准备本地配置后，才在获准部署阶段启用服务。

[`MemoryMax=300M`](https://www.freedesktop.org/software/systemd/man/latest/systemd.resource-control.html) 是服务 cgroup 的 300 MiB 内存限制，无法在限制内回收时可能触发单元内 OOM 终止；它与业务目标 **RSS <150MB** 使用不同口径，不能替代业务内存优化或声称已经达到目标。[`Restart=always`](https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html) 负责进程退出后的自动重启，仍受启动频率限制，显式停止服务时不会自动复起；它不是内存回收算法。RAII、有界队列、任务及时释放与 systemd 最后防线分别验收，不用杀进程或清理脚本代替资源管理。

## 10. 内存日志采集与 PC 日志服务（PATCH-008）

```text
魔百盒 edge-vision.service
    stdout / stderr
        │ StandardOutput=journal / StandardError=journal
        ▼
systemd-journald（默认 namespace）
    Storage=volatile；RuntimeMaxUse=20M，按归档文件回收
        │ _SYSTEMD_UNIT=edge-vision.service
        ▼
独立 Fluent Bit systemd input
    Mem_Buf_Limit=2MB；storage.type=memory；不落盘游标
        │ HTTP JSON 数组；Retry_Limit=1
        ▼
联想 PC :8090/api/v1/logs
    POST：按 PC 接收时刻的 UTC 日期追加日志文件
    GET：读取指定 UTC 日期（含当日）日志的最后 N 行
```

日志采集独立于视频节点和 Nous 事件任务，不由 C++ 自写 HTTP 日志客户端、异步线程或本地轮转文件。Fluent Bit 只读取服务 `_SYSTEMD_UNIT=edge-vision.service` 的 journal 记录，不采集整机日志；记录保留 journal 的消息和结构化字段，HTTP `Format json` 以 JSON 数组批量发送。输入过滤方式以 [Systemd input 官方文档](https://docs.fluentbit.io/manual/data-pipeline/inputs/systemd) 为准，发送格式见 [HTTP output 官方文档](https://docs.fluentbit.io/manual/data-pipeline/outputs/http)。

### 10.1 仅部署模板，尚未启用

- [journald-edge.conf](../deploy/journald-edge.conf) 是未来默认 journal 的 drop-in 模板，安装目标为 `/etc/systemd/journald.conf.d/edge-vision.conf`。`[Journal] Storage=volatile` 使 journal 保存在 `/run/log/journal`，重启不保留；`RuntimeMaxUse=20M` 按 journal 文件空间回收，只删除归档文件，活跃文件及轮转粒度可能使实际占用超过 20M，不能称为严格的实时 20MB 硬限。该配置影响整机默认 namespace，不仅是 edge 服务；不用外部清理脚本补偿。[journald 官方语义](https://www.freedesktop.org/software/systemd/man/latest/journald.conf.html)
- [fluent-bit.conf](../deploy/fluent-bit.conf) 是魔百盒上独立采集进程的配置模板；`Systemd_Filter _SYSTEMD_UNIT=edge-vision.service`、`Read_From_Tail On`、`Mem_Buf_Limit 2MB`、`storage.type memory`。不配置游标 SQLite DB、文件系统缓冲、磁盘积压或拒绝数据落盘。2MB 限制的是输入缓冲阈值，达到阈值后暂停该输入；单次写入可越过阈值，不能当作 Fluent Bit 总 RSS 保证。[内存缓冲](https://docs.fluentbit.io/manual/administration/buffering-and-storage)、[背压行为](https://docs.fluentbit.io/manual/administration/backpressure)
- HTTP 输出使用示例 LAN 主机 `192.168.10.10`、端口 `8090`、URI `/api/v1/logs`、`Format json` 和 `Retry_Limit 1`。最多重试一次，耗尽后丢弃该批次；这是日志传输的有限重试，与 Nous 事件投递不重试的契约分开。重试可能带来重复日志，轮转、采集器重启或网络故障可能丢日志，不承诺必达或精确一次。[重试官方语义](https://docs.fluentbit.io/manual/administration/scheduling-and-retries)

三个部署模板均未在目标机安装或运行。后续获准部署时核对 Armbian 的 systemd／cgroup、journal 读取权限、Fluent Bit 构建包含 systemd input，以及 PC 接收端实现；再验证日志只保留于边缘内存并能投递 PC。不能以模板静态核对代替目标机运行验证，也不把输入缓冲阈值当作任何进程的实测内存指标。

### 10.2 PC 接收、存储与查询

未来 PC `cloud-storage :8090` 的 `POST /api/v1/logs` 接收 Fluent Bit JSON 数组，将每个记录序列化为一行紧凑 JSON，追加至 `cloud-storage/storage/logs/YYYY-MM-DD.log`。文件日期按 **PC 接收到批次时的 UTC 日期** 计算，区别于事件图片按入库生效的 `timestamp_ms` 取日（异常时间按 API 契约回退至 PC 当前时间）；日志文件不写魔百盒，也不插入 `events` 表。运行日志与图片／数据库一起处于 Git 忽略的 `cloud-storage/storage/` 下。

`GET /api/v1/logs` 返回 PC 上指定 UTC 日期文件的最后 N 行，可用当天日期读取当日日志；不会向魔百盒回查。`date`、`tail` 参数、行数范围、返回媒体类型、空文件／缺失文件和错误语义统一以 [api.md](api.md) / [OpenAPI](openapi.yaml) 为准，不在架构另立一套响应。两项接口均属于后续 Python 服务实现，本轮只维护契约与模板。

## 部署前对时与探活（PATCH-009）

魔百盒没有可维持时钟的 RTC 电池时，离线重启可能丢失正确时间。部署流程先通过 SSH 用联想 PC 的系统时间对时，再重启边缘服务。若仍收到异常的事件时间，PC 存储服务按 [API 契约](api.md) 以接收时刻作为事件时间戳，并据此选择图片日期目录。

[scripts/deploy_to_box.sh](../scripts/deploy_to_box.sh) 和 [scripts/start_pc_services.sh](../scripts/start_pc_services.sh) 是后续部署与启动说明的占位。目标机的发行版和运行库基线在部署前读取。持续推理时可从边缘统计观察 SoC 温度，手动处理实测过热；已知限制见 [踩坑记录](troubleshooting.md)。
