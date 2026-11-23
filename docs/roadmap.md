# Edge Vision Analyzer — 里程碑、内部契约与验收

状态：有效设计；全部业务能力待实现。更新日期：2026-11-23（北京时间）。

本文件是阶段交付、节点内部设计和验收标准的正式依据，已合并项目启动规格与 PATCH-001、PATCH-002。开发与 Git 契约以 [AGENTS.md](../AGENTS.md) 为准；系统架构与部署拓扑见 [architecture.md](architecture.md)；接口语义和机器契约分别见 [api.md](api.md)、[openapi.yaml](openapi.yaml)；配置默认值见 [config.json](../config/config.json)。接力开发只需读取仓库文件，不依赖聊天记录或外部附件。

## 1. 当前执行边界与后续启动条件

当前仅初始化公开仓库、目录、文档、示例配置、GitHub Labels / Milestones / 主线保护 / 初始化 Issues，以及 `main` 初始提交和 `feature/capture` 分支。本文列出的交付件、C++ 代码块和验收场景都是后续设计，不表示文件已创建、功能已实现或测试已运行。

本轮不创建 `src/` C++ 实现、不创建单元测试、不运行 CMake / 交叉编译 / CTest、不创建 PR。`CMakeLists.txt` 与 `cmake/aarch64-toolchain.cmake` 属于 `v0.1-oculus` 后续交付，本轮 `cmake/` 仅保留 `.gitkeep`。初始化和分支推送完成后立即停止；后续 Agent 必须取得新的开发授权，不能仅因本文件、Issue 或分支存在而开始实现。

每次获准实现前，先列明“业务模块 → 修改／新增的专属一级模块 → 用户可完成的功能”，明确文件范围、数据归属和验收方式。下文 `src/` 路径表示技术目录与交付位置，不把调用依赖当作业务归属，也不宣称已完成业务模块划分。

## 2. 两阶段数据源与硬件通路

手机经 Type-C 转以太网扩展坞、魔百盒和联想 PC 各用一根网线接入同一以太网交换机；手机与魔百盒不使用 USB 直连。联想 PC 承担 x86_64 Linux 开发、aarch64 交叉编译和 `llama-server :8080` 多模态推理；魔百盒 Armbian 2GB 承担边缘运行，不在其上编译大型 C++ 依赖。

交换机接上级路由器时由 DHCP 获取同网段地址。独立离线组网时手动设置：联想 PC `192.168.10.10`、魔百盒 `192.168.10.11`、手机 `192.168.10.12`，子网掩码均为 `255.255.255.0`。这些是配置示例，实际地址以设备设置为准；物理拓扑正式说明见 [architecture.md](architecture.md)。

### 阶段 A：人工真机验证，最高优先级

1. 手机开启 IP Webcam，提供 HTTP MJPEG 或 RTSP；魔百盒经有线局域网拉流。
2. 后续获准实现并部署 `v0.1-oculus` ARM64 产物后，在魔百盒上验证稳定截帧并保存 `.jpg`。
3. 通过规划中的 `--record-clip assets/demo_clip.mp4 --duration 20` 录制 15～30 秒缓存视频，内容同时包含静止背景与人物活动；直接 remux，不重新编码。
4. 在线切换到该本地视频并稳定循环运行，作为阶段 A 的结束条件。真实手机地址、素材和运行结果由现场验收记录确认，本轮不连接摄像头或录片。

### 阶段 B：无人值守持续开发

阶段 A 验收通过后，通过 [config.json](../config/config.json) 或 `POST /api/v1/source/switch` 将输入切换为 `assets/demo_clip.mp4`，设置 `loop: true`。也可使用具备相应使用许可的开源行人短片。

`v0.2-discern`、`v0.3-nous`、`v1.0-vox` 的开发、联调和压测均基于本地循环视频，不持续依赖手机推流。自动测试使用极小素材或 Mock，不自动下载模型、不启动真实摄像头、不阻塞等待真实 LLM。

## 3. 开源参考与统一处理框架

### 3.1 开源技术基线

下表规定参考目标与需要遵守的模式；它不是已经引入依赖或完成上游版本兼容验证的声明。实际引入时记录所选版本与许可，不另造私有流水线、RTSP 服务端或 JSON / 测试框架。

| 技术目录／职责 | 开源参考 | 本项目约定 |
| --- | --- | --- |
| 流水线框架 | [sherlockchou86/VideoPipe](https://github.com/sherlockchou86/VideoPipe)（原规格仓库名 `video_pipe_c` 的重定向目标） | 参考 `vp_node` / `vp_frame_meta` 模式；本项目自行约定 `BaseNode`、默认容量 2 的丢旧帧有界队列、单工作线程、`attach_next()` 与 `std::shared_ptr<FrameMeta>` 元数据流转，不声称这些全部是上游原生同名 API。 |
| 视觉业务漏斗 | `blakeblackshear/frigate` | 帧差过滤 → 端侧检测 → Event 生命周期聚合 → 异步 GenAI 描述；内存环形事件表与 REST 资源路径。 |
| `capture` | `FFmpeg/FFmpeg`，`doc/examples/demux_decode.c` | 原生 C API 解封装／解码，`sws_scale` 转 RGB24；RTSP 使用 TCP，设置连接／读取超时；原规格中的 `stimeout`、`rw_timeout` 名称须在选定 FFmpeg 版本中确认。不引入 OpenCV。 |
| `detector` | `Tencent/ncnn`、`RangiLyu/nanodet` 的 `demo_ncnn/nanodet.cpp`、`dog-qiuqiu/Yolo-FastestV2` | `ncnn::Net` 加载 `.param` / `.bin`，FP16、ARM NEON、`num_threads = 2`，`from_pixels_resize` 与标准 NMS。 |
| `transport` | `ggml-org/llama.cpp` 的 `tools/server`、`yhirose/cpp-httplib`、`nothings/stb` | 消费原生 `/health` 和 OpenAI 兼容 `/v1/chat/completions`；`stbi_write_jpg_to_func` 内存编码 JPEG，不为正常推理落盘。 |
| `streamer` / `api` | `yhirose/cpp-httplib`、`bluenviron/mediamtx` | `httplib::Server` 提供 REST 与 MJPEG；可选 RTSP 通过 FFmpeg 管道推送外部 MediaMTX，不手写 RTSP 服务端。 |
| 配置／序列化与测试 | `nlohmann/json`、`google/googletest` | JSON 统一使用 `nlohmann::json`；GoogleTest + CTest，纯离线、少量核心用例。 |

### 3.2 节点流转与资源约束

```text
手机 IP Webcam / 本地 demo_clip.mp4
    │ 单次拉流；阶段 A 可同步 remux 录片
    ▼
CaptureNode ──▶ MotionNode ──▶ NcnnDetectorNode ──▶ EventTrackerNode
    FFmpeg       64×48 MAD     仅有运动时检测             │
    RGB24                                               ├──▶ GenAiWorker
                                                        │    异步 llama-server
                                                        ▼          │
                                                   StreamOutNode   │ 回填描述
                                                   OSD + 流输出    ▼
                                                        └──▶ ApiServer :8081
                                                             + EventStore
```

- 所有帧处理阶段继承 `BaseNode` 并通过 `attach_next()` 串联；不在 `main.cpp` 自造散装线程和多重队列。
- 节点输入队列默认容量 2，满时丢弃最旧帧；GenAI 待处理队列容量 1，只保留最新待分析事件。不得无界堆积。
- `FrameMetaPtr` 流转统一元数据；RGB24 图像从采集阶段传下游，不为各检测阶段重复复制整帧。OSD 写入与异步读帧的所有权必须在实现时明确，不能并发修改同一帧缓冲。
- 边缘端常驻 RSS 不超过 300MB；事件环形表容量 100。各阶段的 FPS、耗时、丢帧和 RSS 由统一 metrics 汇总。
- HTTP / LLM 失败不阻塞视频流水线；单后台 GenAI 工作线程只做异步事件分析。

### 3.3 后续规划目录和交付文件

以下包含全部规划文件，并非初始化时已经存在的文件树。当前文件清单以仓库实际内容为准。

```text
edge-vision-analyzer/
├── AGENTS.md
├── CLAUDE.md
├── README.md
├── .gitignore
├── CMakeLists.txt                      # v0.1-oculus 后续实现
├── cmake/aarch64-toolchain.cmake       # v0.1-oculus 后续实现
├── config/config.json
├── docs/
│   ├── architecture.md
│   ├── api.md
│   ├── openapi.yaml
│   └── roadmap.md
├── src/
│   ├── core/
│   │   ├── frame_meta.hpp
│   │   ├── bounded_queue.hpp
│   │   └── base_node.hpp / base_node.cpp
│   ├── common/
│   │   ├── config.hpp / config.cpp
│   │   └── metrics.hpp / metrics.cpp
│   ├── capture/
│   │   ├── capture_node.hpp / capture_node.cpp
│   │   └── frame_dumper.hpp / frame_dumper.cpp
│   ├── detector/
│   │   ├── motion_node.hpp / motion_node.cpp
│   │   └── ncnn_detector_node.hpp / ncnn_detector_node.cpp
│   ├── transport/
│   │   ├── event_store.hpp / event_store.cpp
│   │   ├── event_tracker_node.hpp / event_tracker_node.cpp
│   │   ├── jpeg_base64.hpp / jpeg_base64.cpp
│   │   └── genai_worker.hpp / genai_worker.cpp
│   ├── streamer/
│   │   ├── osd_annotator.hpp / osd_annotator.cpp
│   │   ├── stream_out_node.hpp / stream_out_node.cpp
│   │   └── rtsp_pusher.hpp / rtsp_pusher.cpp
│   ├── api/api_server.hpp / api_server.cpp
│   └── main.cpp
├── models/README.md
├── assets/                            # 现场视频大文件不入库
├── scripts/
│   ├── deploy_to_box.sh
│   └── start_llama_server.sh
├── deploy/edge-vision.service
└── tests/
    ├── fixtures/
    ├── test_capture.cpp
    ├── test_detector.cpp
    ├── test_transport.cpp
    └── test_streamer.cpp
```

### 3.4 `FrameMeta` / `TargetBox` 内部数据契约

以下仅为文档内的规划接口，不在本轮创建头文件。框坐标归一化到 `[0, 1]`；各阶段只填充自己负责的结果字段。

```cpp
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct TargetBox {
    int class_id = -1;
    std::string label;
    float score = 0.0f;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

struct FrameMeta {
    uint64_t frame_id = 0;
    int64_t timestamp_ms = 0;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb_data;  // CaptureNode 填充；下游共享读取
    bool has_motion = false;       // MotionNode 填充
    float motion_score = 0.0f;
    std::vector<TargetBox> targets; // NcnnDetectorNode 填充
    double infer_ms = 0.0;
    std::string active_event_id;    // EventTrackerNode 填充
};

using FrameMetaPtr = std::shared_ptr<FrameMeta>;
```

### 3.5 `BaseNode` 内部接口契约

```cpp
#pragma once
#include "bounded_queue.hpp"
#include "frame_meta.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class BaseNode {
public:
    explicit BaseNode(std::string name, size_t queue_capacity = 2);
    virtual ~BaseNode();
    void attach_next(std::shared_ptr<BaseNode> next);
    void push(FrameMetaPtr meta);
    virtual void start();
    virtual void stop();
    const std::string& name() const { return name_; }

protected:
    virtual FrameMetaPtr process(FrameMetaPtr meta) = 0;
    void emit(FrameMetaPtr meta); // 分发给所有 next_nodes_

private:
    void run_loop();
    std::string name_;
    BoundedQueue<FrameMetaPtr> in_queue_;
    std::vector<std::shared_ptr<BaseNode>> next_nodes_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};
```

`BoundedQueue<FrameMetaPtr>` 必须有固定容量和丢旧帧行为。`BaseNode` 负责统一队列、单工作线程和下游分发；`CaptureNode` 作为源节点运行解封装循环。`start()` / `stop()` 和信号退出应能结束处理与等待，不遗留线程。具体实现与有限测试在获准的对应里程碑中完成。

## 4. 里程碑与具体交付

| 里程碑／规划发版 Tag | 感官意象 | 功能目标 | 当前状态 |
| --- | --- | --- | --- |
| `v0.1-oculus` | Oculus，神目／视觉 | 接入手机有线流和本地循环视频，完成采集与基础控制面 | 待实现 |
| `v0.2-discern` | Discern，洞察／明辨 | 帧差漏斗与 NCNN 端侧目标检测 | 待实现 |
| `v0.3-nous` | Nous，神思／心智 | 事件聚合与异步多模态语义分析 | 待实现 |
| `v1.0-vox` | Vox，神谕之声／宣示 | OSD、MJPEG／RTSP、systemd 守护交付 | 待实现 |

Milestone 的建立不等于已发版；仅在实际完成、验收后按 [AGENTS.md](../AGENTS.md) 执行对应 Tag 的发布流程。

### 4.1 `v0.1-oculus` — 视频采集、录片与基础控制面

**全部交付件：**

- `CMakeLists.txt`、`cmake/aarch64-toolchain.cmake`：支持 x86_64 本机测试构建和 aarch64 交叉编译；均在后续开发创建。
- `src/core/frame_meta.hpp`、`bounded_queue.hpp`、`base_node.hpp`、`base_node.cpp`：统一元数据、丢旧帧队列和节点基类。
- `src/common/config.hpp` / `.cpp`、`metrics.hpp` / `.cpp`：配置加载、原子热更新基础与 FPS／耗时／`/proc/self/statm` RSS 统计。
- `src/capture/capture_node.hpp` / `.cpp`、`frame_dumper.hpp` / `.cpp`：FFmpeg 拉流、本地循环、remux 录片与按间隔保存 JPEG。
- 后续创建 `src/main.cpp` 的最小采集/控制面运行入口与录片命令行装配，使阶段 A 有可运行产物；`v1.0-vox` 再完善完整流水线组装与交付。
- `src/api/api_server.hpp` / `.cpp`：挂载 `GET /api/v1/health`、`GET /api/v1/stats`、`GET /api/v1/config`、`POST /api/v1/source/switch`。
- `tests/test_capture.cpp` 与极小离线 fixture；后续编译、交叉编译和阶段 A 的实际验收记录。

**内部架构：**

`CaptureNode` 对标 FFmpeg `demux_decode.c`：`avformat_open_input` → `avcodec_send_packet` / `avcodec_receive_frame` → `sws_scale` 转 `640×480 RGB24` → `emit(meta)`。单次拉流供后续节点使用；如开启 `--record-clip <path> --duration <sec>`，在解码前把原始 `AVPacket` remux 写入目标 MP4，避免重编码 CPU 开销。

本地文件 `loop == true` 且遇到 `AVERROR_EOF` 时执行 `av_seek_frame` + `avcodec_flush_buffers`；按 `target_fps` 用 `steady_clock` 节流。网络流断线后间隔 2 秒重连。切源失败必须保留原源，返回接口规定的 400 错误；不能先销毁旧源再报告失败。

**离线测试，控制在 3～5 个用例：**

1. 极小本地测试视频能读出帧，RGB24 尺寸和元数据符合契约。
2. EOF 后循环重置，刷新解码器，能继续读帧。
3. 非法路径／源能安全报错，不崩溃。
4. 回环调用 `/health` 可读取控制面存活信息。
5. 回环调用 `/source/switch` 验证切源成功和失败保留原源，可在同一用例包含两次断言。

**阶段 A 真机验收：**

在联想宿主机完成 ARM64 交叉编译；经单独授权部署到魔百盒后，成功拉取手机 IP Webcam 流，按配置保存 `.jpg`，录制含静止与人物运动的 `assets/demo_clip.mp4`，再切换为该视频稳定循环。编译通过、部署完成、现场通路验收分别记录，不能混写为“上线可用”。

### 4.2 `v0.2-discern` — 帧差漏斗与 NCNN 端侧检测

前置条件：阶段 A 已验收，本地循环视频可用。

**全部交付件：**

- `src/detector/motion_node.hpp` / `.cpp`。
- `src/detector/ncnn_detector_node.hpp` / `.cpp`。
- `models/README.md`：YOLO-Fastest-v2 / NanoDet-Plus `.param`、`.bin` 获取与放置路径；模型权重不入仓库，不由测试自动下载。
- `ApiServer` 补充 `PATCH /api/v1/config`，支持原子配置快照热更新；字段、响应与限制见接口契约。
- `tests/test_detector.cpp`，3～5 个纯离线用例。

**内部架构：**

`MotionNode::process(meta)` 步长采样 RGB24 为 `64×48` 灰度图，大小 3072 字节，与上一帧计算 MAD。低于 `motion_mad_threshold` 时写 `has_motion = false`，静态帧继续透传，但跳过检测推理。

`NcnnDetectorNode::process(meta)` 仅在 `has_motion == true` 时调用 `ncnn::Net`。加载模型 `.param` / `.bin`，开启 `use_fp16_arithmetic = true`、ARM NEON，设置 `num_threads = 2`，输入 `320×320`；通过 `from_pixels_resize` 预处理，按 `target_labels`、`conf_threshold` 过滤并执行标准 NMS，写入 `targets` 和 `infer_ms`。

热更新只覆盖 `detector.motion_mad_threshold`、`detector.conf_threshold`、`llm.enabled`、`llm.cooldown_sec`，不把其他配置隐式开放为热更新接口。内部调用信任既定 struct / interface，外部输入按接口契约处理，不堆砌重复防御。

**离线测试，规划 4 个用例：**

1. 静止图被 MAD 漏斗拦截，不触发检测推理。
2. 变化图超过阈值，放行检测。
3. 空帧安全透传。
4. 检测结果按目标类别和阈值过滤，NMS 输出符合预期。

基于本地循环视频记录帧差跳过率、检测 FPS、推理耗时与 RSS，作为本阶段实际结果；没有实测时不填造指标。

### 4.3 `v0.3-nous` — 事件聚合与异步 `llama-server` 协同

前置条件：本地循环视频与端侧检测链路稳定。

**全部交付件：**

- `scripts/start_llama_server.sh`：联想 PC 启动多模态 `llama-server` 的标准脚本。
- `src/transport/event_store.hpp` / `.cpp`：容量 100 的线程安全内存环形事件表。
- `src/transport/event_tracker_node.hpp` / `.cpp`：事件生命周期和触发节流。
- `src/transport/jpeg_base64.hpp` / `.cpp`：JPEG 内存编码与 Base64。
- `src/transport/genai_worker.hpp` / `.cpp`：异步上游客户端。
- `ApiServer` 补充 `GET /api/v1/events`。
- `tests/test_transport.cpp`，3～5 个离线用例；HTTP 使用 Mock 或回环桩服务，不依赖真实模型。

**内部架构：**

`EventTrackerNode::process(meta)` 参考 Frigate Event Lifecycle，按事件聚合，不逐帧请求 LLM：

1. 检测到目标、没有活跃事件且满足 `llm.cooldown_sec`，创建新 Event，写入 `EventStore`，把代表帧异步交给 `GenAiWorker`。
2. 连续出现目标只更新活跃事件 `end_ts_ms` 和最高置信度检测框，不重复触发 LLM。
3. 连续超过 `event.end_grace_sec`（默认 5 秒）无目标，将事件置为 `ended`。生命周期为 new / update / end，查询表示遵循接口定义。

`GenAiWorker` 使用单后台线程和容量 1 的 `BoundedQueue`，只保留最新待分析事件。用 `stbi_write_jpg_to_func` 以默认质量 70 压缩 JPEG → Base64 → `httplib::Client::Post`。连接超时 3 秒、读取超时 25 秒；成功后按 `event_id` 回填 `EventStore` 的 `llm_summary` 与耗时。

严格消费联想 PC 的原生 `GET /health` 和 `POST /v1/chat/completions`，不修改上游服务端。图像为 `data:image/jpeg;base64,...`，请求遵循 OpenAI Vision 消息结构，使用配置中的模型与中文提示词；启动规格的请求参数为 `max_tokens: 128`、`temperature: 0.2`。解析 `choices[0].message.content`，精确请求格式见 [api.md](api.md)。

失败只记录必要日志，不阻塞帧处理；可选保存到 `failed_frames/`，最多 50 张循环覆盖，不无界占用内存或磁盘。环形事件可能被淘汰，异步结果回填必须对应原 `event_id`，不误写其他事件。

**离线测试，规划 4 个用例：**

1. 连续多帧含目标只生成 1 个 Event，且仅触发 1 次 LLM 请求。
2. 冷却期内再次触发被拦截。
3. OpenAI Vision 请求 JSON 组装正确，响应内容能按契约解析。
4. HTTP 连接／读取超时不崩溃，不阻塞视频流水线。

后续真实联调另行记录 `llama-server` 可达性、事件数量、描述和 LLM 延迟；离线测试通过不替代真实推理验证。

### 4.4 `v1.0-vox` — OSD、双模推流与守护交付

前置条件：本地循环视频、检测、事件与异步描述链路可用。

**全部交付件：**

- `src/streamer/osd_annotator.hpp` / `.cpp`。
- `src/streamer/stream_out_node.hpp` / `.cpp`。
- `src/streamer/rtsp_pusher.hpp` / `.cpp`。
- `src/main.cpp`：声明式 `BaseNode::attach_next()` 组装与 `SIGINT` / `SIGTERM` 优雅退出。
- `deploy/edge-vision.service`、`scripts/deploy_to_box.sh`。
- `ApiServer` 补充 `GET /api/v1/snapshot.jpg` 和 `GET /api/v1/stream`。
- `tests/test_streamer.cpp`，3～5 个离线用例。
- 完善 `README.md`：架构图、部署步骤、实测性能指标表；发布／部署／现场验收结果分别记录。

**内部架构：**

`OsdAnnotator` 直接处理 RGB24，绘制 2px 矩形框与内置 `8×8` ASCII 点阵标签，例如 `person 0.87`；不依赖 OpenCV 或外部字体。归一化框转像素坐标后须裁剪到画面边界。

`StreamOutNode` 默认采用 `mjpeg`：只在存在活跃 `/api/v1/stream` 连接或收到 `/api/v1/snapshot.jpg` 请求时调用 `stbi_write_jpg_to_func`，广播带 OSD 的图像；无人观看且无快照请求时跳过 JPEG 编码。MJPEG 媒体类型为 `multipart/x-mixed-replace; boundary=frame`；快照无首帧时返回 503。

可选 `rtsp` 模式由 `RtspPusher` 启动 FFmpeg 管道，以 `-preset ultrafast -tune zerolatency -b:v 500k` 推送外部 `bluenviron/mediamtx`；不在边缘程序里实现 RTSP 服务端。选择模式、地址和端口从统一配置读取。

**离线测试，规划 3 个用例：**

1. 越界检测框被裁剪，绘制不越界。
2. 无客户端且无快照请求时跳过 JPEG 编码。
3. 快照更新和流输出启停状态机符合契约；用本地桩对象验证，不连接真实摄像头或 MediaMTX。

后续经授权在魔百盒部署并验收 systemd 启停、信号退出、MJPEG／选定 RTSP 入口与资源占用；仅有二进制或 service 文件不代表服务已部署。

## 5. 统一验收与接力记录

- 每个核心模块仅保留 3～5 个 GoogleTest 用例；全部 CTest 测试应在 5 秒内完成。不为 getter / setter、纯数据结构和日志打印编写测试。
- 单元测试只使用 `tests/fixtures/` 的极小素材、Mock 或回环桩；不自动下载权重，不等候真实摄像头或 LLM。真实硬件验收单独执行、单独记证据。
- 常规开发只按 [AGENTS.md](../AGENTS.md) 执行必要构建与 CTest，不新增环境自检、摘要校验或重复 wrapper。本轮初始化仍禁止构建和测试。
- 遇到配置、交叉编译、链接、部署或测试错误，按 AGENTS 的 `type:bug` + `agent-log` Issue 四项格式记录；公开日志不包含密钥、真实私人影像或私人路径。
- 后续交接至少记录：任务／Issue、获准范围、分支与完整提交 SHA、已改文件、实际运行的验证及结果、未完成项和下一步。状态明确区分“已修改／已验证／已构建／已部署／线上已验收”。
- 里程碑完成后更新本文件的实际状态与对应证据，接口变化同步 api / openapi，配置变化同步 config；不要把计划测试和预期指标写成已经发生的结果。
