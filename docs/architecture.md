# Edge Vision Analyzer 架构

状态：有效设计契约，全部能力待实现。维护：项目接力 Agent；日期：2026-11-23（北京时间）。本文与 [roadmap](roadmap.md)、[api](api.md) / [OpenAPI](openapi.yaml)、[AGENTS](../AGENTS.md) 共同构成仓库自包含依据；不依赖外部聊天记录。

## 1. 物理拓扑

```text
[手机 IP Webcam] ──(Type-C 转网口扩展坞)──┐
                                         │
[魔百盒 Armbian 2GB] ────(网线)───────────┼──▶ [以太网交换机] ──▶ (可选：上级路由器 DHCP)
                                         │
[联想奔腾 PC 16GB] ──────(网线)───────────┘
  (交叉编译 + llama-server :8080)
```

所有设备接入同一有线局域网；手机与魔百盒不使用 USB 直连。手机扩展坞若有 PD 供电口，可独立供电并持续推流。该方案避免手机充电/通信占用魔百盒 USB 供电和额外 USB 网络子网；实际延迟、稳定性与供电能力待现场验收，不宣称零延迟或绝对稳定。

- 交换机接上级路由器：三台设备通过 DHCP 获取同网段地址，按租约更新配置中的手机和 PC 地址。
- 交换机离线独立使用：需手动设置同网段静态 IP；纯交换机不提供 DHCP。示例联想 `192.168.10.10`、魔百盒 `192.168.10.11`、手机 `192.168.10.12`，掩码 `255.255.255.0`。离线局域网通信不需要网关和 DNS。
- 手机视频地址示例为 `http://192.168.10.12:8080/video`；PC 上游为 `http://192.168.10.10:8080`；边缘控制端监听 `0.0.0.0:8081`。手机的实际流路径/以太网设置能力需真机确认。

## 2. 机器职责与资源预算

| 设备 | 职责与约束 |
| --- | --- |
| 联想盒子电脑 | 2021 年奔腾 6 系列，x86_64 Linux，16GB DDR3；开发、本机离线测试、aarch64 交叉编译、常驻多模态 llama-server |
| 魔百盒 | Amlogic ARMv8 四核 Cortex-A53，Armbian，2GB RAM；只运行边缘产物，禁止在目标机编译大型 C++ 依赖 |
| 手机 | IP Webcam 提供 HTTP MJPEG 或 RTSP 视频，经有线扩展坞接入交换机 |

边缘程序 RSS **≤300MB**。所有节点输入队列有界，默认容量 **2**，满时丢弃最旧帧；GenAI 待处理事件队列容量 **1**，只保留最新任务。事件表容量 **100**；可选失败帧落盘最多 **50** 张循环覆盖。检测线程 **2**，默认输入解码 **640×480 RGB24 / 15 FPS**，检测输入 **320×320**，帧差灰度图 **64×48**。这些是目标与硬约束，并非当前测量结果。

## 3. 软件数据流

```text
手机有线流 / 本地 demo_clip.mp4（循环）
      │ 单次拉流；阶段 A 同时 remux 原始 AVPacket 录片
      ▼
CaptureNode（FFmpeg → sws_scale → 640×480 RGB24）
      │ shared_ptr<FrameMeta>；有界队列默认 2，满则丢旧
      ▼
MotionNode（64×48 灰度 MAD，静态帧透传并标记跳过检测）
      ▼
NcnnDetectorNode（仅 has_motion=true 时 NCNN 推理与 NMS）
      ▼
EventTrackerNode（new / update / end 生命周期）
      ├────事件任务────▶ GenAiWorker（后台线程，队列 1）
      │                         │ JPEG 内存编码 → Base64 → llama-server
      ▼                         │ 结果按 event_id 回填
StreamOutNode（OSD / 按需 MJPEG / 可选 mediamtx RTSP）
      │                         ▼
      └──────────▶ ApiServer :8081 + EventStore（环形 100）
```

所有处理阶段遵循 `BaseNode` + `attach_next()`，每个节点单工作线程；不在 main 中拼接散装线程与多重队列。元数据以 `std::shared_ptr<FrameMeta>` 流转，下游不复制/改写 capture 填充的 RGB 原始缓冲。OSD 阶段按需生成独立输出画面，避免与异步 GenAI 读同帧时产生数据竞争；事件任务持有受限生命周期的帧引用。API 不持有不断增长的历史帧。`FrameMeta` / `TargetBox` / `BaseNode` 的完整设计契约见 [roadmap](roadmap.md)。

FrameMeta 包括帧编号、毫秒时间戳、尺寸、RGB 数据、运动标记/分数、目标列表、检测耗时及活动事件 ID；TargetBox 为类别、标签、置信度和 `[0,1]` 归一化坐标。分叉到异步任务后应明确只读字段与写入时机，不能把 shared_ptr 当作线程同步机制。

## 4. 开源参考与本项目约束

| 技术目录 | 开源基线（原始参考） | 采用的模式 / 本项目约束 |
| --- | --- | --- |
| core | [VideoPipe](https://github.com/sherlockchou86/VideoPipe)（原 video_pipe_c 地址已迁移） | 参考节点连接和元数据流转模式；本项目 BaseNode、默认容量 2 与丢旧策略为自身契约，不宣称是上游原生 API |
| detector / transport | [Frigate](https://github.com/blakeblackshear/frigate) | 帧差 → 检测 → 事件聚合 → 异步 GenAI 四级漏斗；内存环形事件表与 REST 资源风格 |
| capture | [FFmpeg demux_decode.c](https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/demux_decode.c) | 原生 C API 解封装/解码，sws_scale 转 RGB24，不引入 OpenCV；RTSP TCP 与 I/O 超时 |
| detector | [NCNN](https://github.com/Tencent/ncnn)、[NanoDet](https://github.com/RangiLyu/nanodet/blob/main/demo_ncnn/nanodet.cpp)、[YOLO-FastestV2](https://github.com/dog-qiuqiu/Yolo-FastestV2) | ncnn::Net 加载 .param/.bin，FP16、ARM NEON、2 线程，from_pixels_resize 与 NMS |
| transport | [llama-server](https://github.com/ggml-org/llama.cpp/tree/master/tools/server)、[cpp-httplib](https://github.com/yhirose/cpp-httplib)、[stb](https://github.com/nothings/stb) | 原生 /health 与 /v1/chat/completions，stbi_write_jpg_to_func 内存编码，零临时图像落盘 |
| streamer / api | [cpp-httplib](https://github.com/yhirose/cpp-httplib)、[MediaMTX](https://github.com/bluenviron/mediamtx) | REST 与 multipart MJPEG；RTSP 通过 FFmpeg 管道推外部 mediamtx，不自写 RTSP 服务端 |
| common / tests | [nlohmann/json](https://github.com/nlohmann/json)、[GoogleTest](https://github.com/google/googletest) | JSON 统一序列化；GoogleTest + CTest 离线测试 |

这些是后续实现参考，当前未引入或复制上游源码，依赖版本与许可证随实现登记。FFmpeg 版本冻结时按[官方协议文档](https://ffmpeg.org/ffmpeg-protocols.html)核对 `rtsp_transport=tcp`、`rw_timeout` 及旧 `stimeout` 与所选版本 `timeout` 参数的适用性，不能假设跨版本参数完全相同。llama-server 调用采用其[原生服务契约](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)，外部响应解析见 api 文档。

## 5. 目录与责任

```text
src/{core,common,capture,detector,transport,streamer,api}/
cmake/  config/  docs/  models/  assets/  scripts/  deploy/  tests/fixtures/
```

当前仅为技术目录占位，未创建 C++ 或单元测试。目录名不自动等于业务模块或治理中的一级模块；不得据此宣称多个业务共享同一个一级模块。未来每次实施前须按 AGENTS 列清业务归属、专属一级模块、影响范围和功能结果，不把技术依赖写成归属关系。

- capture：源接入、循环、录片和截帧的技术实现范围。
- detector：帧差与 NCNN 检测范围。
- transport：事件表、生命周期、JPEG/Base64 与异步语义推理范围。
- streamer：OSD 与输出范围；api 提供控制面并调用明确接口。
- core/common：当前是待实现的框架和配置契约目录。业务一级模块最终归属清单尚未实施；若与独占规则有冲突，先形成拆分方案，再获对应实现指令，不能借本轮初始化默认放开共享。

## 6. 数据源、里程碑和验收

阶段 A 优先完成手机有线流 → 截帧 JPEG → 15–30 秒原包 remux 录片 → 切换本地循环；阶段 B 使用本地循环素材完成后续无人值守开发。详尽交付与每模块 3–5 个离线验收点集中维护于 [roadmap](roadmap.md)。

| 里程碑 / 后续发版 Tag | 责任目标 |
| --- | --- |
| v0.1-oculus（神目） | 采集录片与基础控制面 |
| v0.2-discern（洞察） | 帧差与 NCNN 检测 |
| v0.3-nous（神思） | 事件与异步多模态推理 |
| v1.0-vox（神谕之声） | OSD、视频输出和 systemd 交付 |

GenAI 连接/读取超时分别 **3 / 25 秒**，JPEG 质量 **70**，事件无目标宽限 **5 秒**、调用冷却 **15 秒**；LLM 失败不能阻塞视频流水线。默认 MJPEG 仅在客户端观看或快照请求时编码，无观看和请求时不编码；可选 RTSP 使用 `-preset ultrafast -tune zerolatency -b:v 500k`。录片不重编码，格式兼容性与真实手机输出需阶段 A 验收，不在离线 CI 中自动等设备。

接口路径、错误体、热更新和脱敏以 [api](api.md) / [OpenAPI](openapi.yaml) 为准；示例配置在 [config.json](../config/config.json)。后续交付前需验证 SIGINT/SIGTERM 优雅停止、节点队列清理、异步工作线程退出及 RSS 预算。
