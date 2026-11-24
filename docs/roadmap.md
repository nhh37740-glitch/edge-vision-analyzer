# Edge Vision Analyzer — 里程碑、内部契约与验收

状态：有效设计；v0.1 的核心框架、采集模块与控制面已实现并通过本地回环验证，v0.2 的帧差漏斗 MotionNode、NcnnDetectorNode 与检测量化选型模块已实现，v0.3 的 JPEG／Base64 编码、`GenAiWorker` 异步投递、`EventTrackerNode` 事件聚合与 LLM 量化选型已实现，v1.0 的 OSD、快照与 MJPEG／RTSP 双模输出已实现，上述模块已由 `src/main.cpp` 接入端到端运行入口；PC 云存储服务已实现，health 上游探活已实现，部署脚本与模板已就绪，PATCH config 原子热更新已实现；离线单测已覆盖上述模块。2026-11-24 已在魔百盒完成本地视频真机部署验收，手机流、LLM／存储联动、RTSP／MediaMTX 与日志采集仍待验收。更新日期：2026-11-24（北京时间）。

本文件是阶段交付、节点内部设计和验收标准的正式依据，已合并项目启动规格与 PATCH-001 至 PATCH-009。存储和历史查询以 PATCH-003 为准。仓库协作和构建契约以 PATCH-004/005 为准。开发与 Git 契约以 [AGENTS.md](../AGENTS.md) 为准；系统架构与部署拓扑见 [architecture.md](architecture.md)；接口语义和机器契约分别见 [api.md](api.md)、[openapi.yaml](openapi.yaml)；配置默认值见 [config.example.json](../config/config.example.json)。接力开发只需读取仓库文件，不依赖聊天记录或外部附件。

## 1. 当前执行边界与后续启动条件

当前已实现 C++17 构建骨架、aarch64 工具链、`src/core`／`src/common` 的节点框架、配置与指标，`src/capture` 的 FFmpeg 拉流、RGB24 解码、截帧和仅视频 remux 录片，`src/streamer` 的 OSD 标注与按需 JPEG／MJPEG／RTSP 输出，`src/transport` 的 JPEG／Base64 编码、`GenAiWorker` 异步投递与 `EventTrackerNode` 事件聚合，以及 `src/api` 的 health、stats、config 读取与 PATCH 原子热更新、source-switch 控制面；上述模块均有离线测试覆盖，并已由 `src/main.cpp` 装配为端到端运行入口。PC 存储服务已实现，部署脚本与模板已就绪；魔百盒已用本地视频完成首次真机部署验收，手机流、LLM／存储联动、RTSP／MediaMTX 与日志采集仍待验收。所有修改以绑定 Issue 的 PR 走 `ci-gate` 并按 Squash 合并。

当前仓库包含协作模板、GitHub Actions、根 [CMakeLists.txt](../CMakeLists.txt) 的 C++17 构建骨架和 `cmake/aarch64-toolchain.cmake`；Issue #1 已使 `edge_core` 与 `test_build` 可构建，Issue #2 增加 `edge_common` 与 `test_core`，Issue #3 增加 `edge_capture`，Issue #4 增加 `edge_api` 与运行入口，Issue #5 增加 `test_capture` 与 `test_api` 离线用例；其余目标随 `v0.1-oculus` 的各项 Issue 加入。FastAPI `server.py` 已在 `v0.3-nous` 实现。接手 Agent 在收到开发指令后实施业务，所有修改以绑定 Issue 的 PR 走 `ci-gate` 并按 Squash 合并。

每次获准实现前，先列明“业务模块 → 修改／新增的专属一级模块 → 用户可完成的功能”，明确文件范围、数据归属和验收方式。边缘实时视觉独占 `src/` 技术范围，PC 事件存储独占 `cloud-storage/` 及历史数据；两者经 HTTP 契约协作，不互相借用一级模块。具体责任说明以 [architecture](architecture.md) 为准。

## 2. 两阶段数据源与硬件通路

手机经 Type-C 转以太网扩展坞、魔百盒和联想 PC 各用一根网线接入同一以太网交换机；手机与魔百盒不使用 USB 直连。联想 PC 承担 x86_64 Linux 开发、aarch64 交叉编译，并以独立进程运行 `llama-server :8080` 和 `cloud-storage :8090`；魔百盒 Armbian 2GB 无状态运行，常驻 RSS 必须 <150MB，不在其上编译大型 C++ 依赖。

交换机接上级路由器时由 DHCP 获取同网段地址。独立离线组网时手动设置：联想 PC `192.168.10.10`、魔百盒 `192.168.10.11`、手机 `192.168.10.12`，子网掩码均为 `255.255.255.0`。这些是配置示例，实际地址以设备设置为准；物理拓扑正式说明见 [architecture.md](architecture.md)。

### 阶段 A：人工真机验证，最高优先级

1. 手机开启 IP Webcam，提供 HTTP MJPEG 或 RTSP；魔百盒经有线局域网拉流。
2. 后续获准实现并部署 `v0.1-oculus` ARM64 产物后，在魔百盒上验证稳定截帧并保存 `.jpg`。
3. 通过已实现的 `--record-clip assets/demo_clip.mp4 --duration 20` 录制 15～30 秒缓存视频，内容同时包含静止背景与人物活动；直接 remux，不重新编码，输出仅保留视频轨。
4. 在线切换到该本地视频并稳定循环运行，作为阶段 A 的结束条件。上述截帧与录片只在用户显式执行阶段 A 时保留，不扩展为常态历史留存或失败补偿。真实手机地址、素材和运行结果由现场验收记录确认，本轮不连接摄像头或录片。

### 阶段 B：无人值守持续开发

阶段 A 验收通过后，编辑由 [config.example.json](../config/config.example.json) 复制出的 `config/config.local.json`，或调用 `POST /api/v1/source/switch`，将输入切换为 `assets/demo_clip.mp4`，设置 `loop: true`。也可使用具备相应使用许可的开源行人短片。

`v0.2-discern`、`v0.3-nous`、`v1.0-vox` 的开发、联调和压测均基于本地循环视频，不持续依赖手机推流。自动测试使用极小素材或 Mock，不自动下载模型、不启动真实摄像头、不阻塞等待真实 LLM。

## 3. 开源参考与统一处理框架

### 3.1 开源技术基线

下表规定参考目标与需要遵守的模式；它不是已经引入依赖或完成上游版本兼容验证的声明。实际引入时记录所选版本与许可，不另造私有流水线、RTSP 服务端或 JSON / 测试框架。

| 技术目录／职责 | 开源参考 | 本项目约定 |
| --- | --- | --- |
| 流水线框架 | [sherlockchou86/VideoPipe](https://github.com/sherlockchou86/VideoPipe)（原规格仓库名 `video_pipe_c` 的重定向目标） | 参考 `vp_node` / `vp_frame_meta` 模式；本项目自行约定 `BaseNode`、默认容量 2 的丢旧帧有界队列、单工作线程、`attach_next()` 与 `std::shared_ptr<FrameMeta>` 元数据流转，不声称这些全部是上游原生同名 API。 |
| 视觉业务漏斗 | [Frigate](https://github.com/blakeblackshear/frigate) | 帧差过滤 → 端侧检测 → 活跃事件瞬时聚合 → 异步 GenAI 描述；历史记录归 PC。 |
| 边缘／存储分离 | [Kerberos Agent](https://github.com/kerberos-io/agent)、[Kerberos Vault](https://github.com/kerberos-io/vault) | 参考边缘处理与独立集中存储的职责分离；本项目自定无状态边缘、FastAPI／SQLite 和 HTTP 契约，不声称上游本身禁止本地录制或采用相同实现。 |
| `capture` | `FFmpeg/FFmpeg`，`doc/examples/demux_decode.c` | 原生 C API 解封装／解码，`sws_scale` 转 RGB24；RTSP 使用 TCP，设置连接／读取超时；原规格中的 `stimeout`、`rw_timeout` 名称须在选定 FFmpeg 版本中确认。不引入 OpenCV。 |
| `detector` | `Tencent/ncnn`、`RangiLyu/nanodet` 的 `demo_ncnn/nanodet.cpp`、`dog-qiuqiu/Yolo-FastestV2` | `ncnn::Net` 加载 `.param` / `.bin`，FP16、ARM NEON、`num_threads = 2`，`from_pixels_resize` 与标准 NMS。 |
| `transport` | `ggml-org/llama.cpp` 的 `tools/server`、`yhirose/cpp-httplib`、`nothings/stb` | 原生 LLM 接口；`stbi_write_jpg_to_func` 一次编码干净 JPEG，单线程顺序完成可选推理与可选 PC 存储投递，边缘不落盘。 |
| `streamer` / `api` | `yhirose/cpp-httplib`、`bluenviron/mediamtx` | `httplib::Server` 提供 REST 与 MJPEG；可选 RTSP 通过 FFmpeg 管道推送外部 MediaMTX，不手写 RTSP 服务端。 |
| 配置／序列化与测试 | `nlohmann/json`、`google/googletest` | JSON 统一使用 `nlohmann::json`；GoogleTest + CTest，纯离线、少量核心用例。 |

### 3.2 节点流转与资源约束

```text
手机 IP Webcam / 本地 demo_clip.mp4
    │ 单次拉流；阶段 A 可同步仅视频 remux 录片
    ▼
CaptureNode ──▶ MotionNode ──▶ NcnnDetectorNode ──▶ EventTrackerNode
    FFmpeg       64×48 MAD     仅有运动时检测             │ 仅活跃事件瞬时状态
    RGB24                                               ├──▶ GenAiWorker / Nous
                                                        │    单线程，队列 1
                                                        │    干净 JPEG 质量70编码一次
                                                        │    1. 可选 LLM :8080
                                                        │    2. 可选 Storage :8090
                                                        ▼          │
                                                   StreamOutNode   ▼
                                                   OSD + 流输出    PC cloud-storage
                                                        │          images + SQLite
                                                        ▼          历史查询
                                                   ApiServer :8081
                                                   实时控制/快照/流
```

- 所有帧处理阶段继承 `BaseNode` 并通过 `attach_next()` 串联；不在 `main.cpp` 自造散装线程和多重队列。
- 节点输入队列默认容量 2，满时丢弃最旧帧；GenAI 待处理队列容量 1，只保留最新待分析事件。不得无界堆积。
- `FrameMetaPtr` 流转统一元数据；RGB24 图像从采集阶段传下游，不为各检测阶段重复复制整帧。OSD 写入与异步读帧的所有权必须在实现时明确，不能并发修改同一帧缓冲。
- 边缘端常驻 RSS 必须 <150MB；仅保留当前活跃事件的瞬时 ID、时间、最佳目标等聚合状态，不维护本地历史表、历史图片或失败任务磁盘队列。各阶段 FPS、耗时、丢帧和 RSS 由统一 metrics 汇总。
- 单后台 Nous 工作线程先可选 LLM、后可选存储；LLM 失败空描述降级，存储失败释放任务。不重试、不积压、不阻塞视频流水线。

### 3.3 后续规划目录和交付文件

以下包含全部规划文件，并非初始化时已经存在的文件树。当前文件清单以仓库实际内容为准。

```text
edge-vision-analyzer/
├── AGENTS.md
├── CLAUDE.md
├── README.md
├── .gitignore
├── CMakeLists.txt                      # C++17 骨架，edge_core 可构建
├── cmake/aarch64-toolchain.cmake       # 已创建
├── config/config.example.json
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
│   └── start_pc_services.sh
├── deploy/
│   ├── edge-vision.service
│   ├── journald-edge.conf
│   └── fluent-bit.conf
├── cloud-storage/                      # 与 ARM64 构建解耦
│   ├── README.md                       # 本轮契约与后续启动说明
│   ├── requirements.txt                # fastapi / uvicorn / pydantic
│   ├── schema.sql                      # 本轮 SQLite 表与索引契约
│   ├── server.py                       # FastAPI + SQLite 服务（已实现）
│   └── storage/                        # 运行时整体忽略，不入库
│       ├── images/YYYY-MM-DD/<event_id>.jpg
│       └── events.db
└── tests/
    ├── fixtures/
    ├── test_build.cpp
    ├── test_core.cpp
    ├── test_capture.cpp
    ├── test_api.cpp
    ├── test_detector.cpp
    ├── test_transport.cpp
    └── test_streamer.cpp
```

### 3.4 `FrameMeta` / `TargetBox` 内部数据契约

以下契约已落地于 `src/core/frame_meta.hpp`。框坐标归一化到 `[0, 1]`；各阶段只填充自己负责的结果字段。

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace edge {

struct TargetBox {
  int class_id = -1;
  std::string label;
  float score = 0.0f;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

struct FrameMeta {
  std::uint64_t frame_id = 0;
  std::int64_t timestamp_ms = 0;
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgb_data;  // Filled by CaptureNode; read-only downstream.
  bool has_motion = false;             // Filled by MotionNode.
  float motion_score = 0.0f;
  std::vector<TargetBox> targets;  // Filled by NcnnDetectorNode.
  double infer_ms = 0.0;
  std::string active_event_id;  // Filled by EventTrackerNode.
};

using FrameMetaPtr = std::shared_ptr<FrameMeta>;

}  // namespace edge
```

### 3.5 `BaseNode` 内部接口契约

```cpp
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/bounded_queue.hpp"
#include "core/frame_meta.hpp"
#include "core/node_state.hpp"

namespace edge {

class BaseNode {
 public:
  explicit BaseNode(std::string name, std::size_t queue_capacity = 2);
  virtual ~BaseNode();

  BaseNode(const BaseNode&) = delete;
  BaseNode& operator=(const BaseNode&) = delete;

  void attach_next(std::shared_ptr<BaseNode> next);
  void push(FrameMetaPtr meta);
  void start();
  void stop();

  bool running() const;
  NodeState state() const;
  const std::string& name() const;
  std::uint64_t dropped_frames() const;

  // 进程内所有节点的累计丢帧；stats 与该值合并。
  static std::uint64_t total_dropped_frames();

 protected:
  virtual FrameMetaPtr process(FrameMetaPtr meta);  // 默认透传
  virtual void run();
  void emit(FrameMetaPtr meta);
  bool stop_requested() const;
  void set_state(NodeState state);

 private:
  void record_dropped_frame();

  std::string name_;
  BoundedQueue<FrameMetaPtr> input_queue_;
  std::atomic<std::uint64_t> dropped_frames_{0};
  std::vector<std::shared_ptr<BaseNode>> next_nodes_;
  std::mutex next_mutex_;
  std::mutex lifecycle_mutex_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<NodeState> state_{NodeState::kStopped};
  bool stopped_once_ = false;
};

}  // namespace edge
```

`BoundedQueue<FrameMetaPtr>` 具有固定容量和丢旧帧行为。`BaseNode` 负责统一队列、单工作线程和下游分发；`CaptureNode` 作为源节点运行解封装循环。节点状态定义在 `src/core/node_state.hpp`；`start()` / `stop()` 与信号退出会结束处理和等待，且 `stop()` 后再 `start()` 为空操作。节点在入队丢旧和停止时丢弃队列帧都会累计 `dropped_frames`，`stats` 合并 `Metrics` 与 `BaseNode::total_dropped_frames()`（Issue #55）。上述契约已由 `src/core/base_node.cpp`、`src/core/bounded_queue.hpp` 实现，`test_core` 覆盖丢旧、丢帧计数、串联、停止 join 与重复启动。

## 4. 里程碑与具体交付

| 里程碑／规划发版 Tag | 感官意象 | 功能目标 | 当前状态 |
| --- | --- | --- | --- |
| `v0.1-oculus` | Oculus，神目／视觉 | 接入手机有线流和本地循环视频，完成采集与基础控制面 | 已实现；魔百盒本地视频与控制面真机验收完成，手机流阶段 A 待执行 |
| `v0.2-discern` | Discern，洞察／明辨 | 帧差漏斗与 NCNN 端侧目标检测 | 检测与量化选型、PATCH config 热更新已实现；魔百盒本地视频真机验收完成 |
| `v0.3-nous` | Nous，神思／心智 | 瞬时事件聚合、异步推理／存储投递、PC 历史查询 | 事件聚合、异步投递、LLM 量化选型与 PC 云存储已实现，云存储由离线 pytest 覆盖；边缘与 LLM／存储联动待真机验收 |
| `v1.0-vox` | Vox，神谕之声／宣示 | OSD、MJPEG／RTSP、systemd 守护交付 | OSD、快照、MJPEG 与 systemd 已在魔百盒验收；RTSP／MediaMTX 与日志采集待验收 |

验收完成后按 [AGENTS.md](../AGENTS.md) 发布。任一 `v*` Tag 都要求 x86_64 构建及 CTest、云存储离线 pytest、aarch64 构建及打包。打 Tag 前确认云存储已有可运行测试、`deploy/edge-vision.service` 模板已在目标机验收，且 CMake 可安装 `bin/edge-vision-analyzer`。这是首个 Release 同样适用的前置条件。

### 4.1 `v0.1-oculus` — 视频采集、录片与基础控制面

**全部交付件：**

- [CMakeLists.txt](../CMakeLists.txt) 已建立实际静态库、运行与测试目标；`cmake/aarch64-toolchain.cmake` 已在 Issue #1 创建，后续 Issue 继续在 v0.2+ 加入对应模块目标。支持 x86_64 本机测试和 aarch64 交叉编译。依赖矩阵见 [架构构建契约](architecture.md)。
- `src/core/frame_meta.hpp`、`bounded_queue.hpp`、`base_node.hpp`、`base_node.cpp`：统一元数据、丢旧帧队列和节点基类。
- `src/common/config.hpp` / `.cpp`、`metrics.hpp` / `.cpp`：配置加载、原子热更新基础与 FPS／耗时／`/proc/self/statm` RSS 统计。`soc_temp_c` 仅用于压测观测。
- `src/capture/capture_node.hpp` / `.cpp`、`frame_dumper.hpp` / `.cpp`：FFmpeg 拉流、本地循环、仅视频 remux 录片与按间隔保存 JPEG。
- `src/main.cpp` 已实现最小采集/控制面运行入口与 `--record-clip`／`--duration` 录片命令行装配；`v1.0-vox` 再完善完整流水线组装与交付。
- `src/api/api_server.hpp` / `.cpp`：挂载 `GET /api/v1/health`、`GET /api/v1/stats`、`GET /api/v1/config`、`POST /api/v1/source/switch`。
- `tests/test_capture.cpp`（当前 2 个用例）、`tests/test_api.cpp` 与极小离线 fixture；本机已构建并运行离线测试，Issue #51 补充采集用例。目标机已用离线原生构建完成本地视频验收；aarch64 交叉编译与手机流阶段 A 验收待补。

**内部架构：**

`CaptureNode` 对标 FFmpeg `demux_decode.c`：`avformat_open_input` → `avcodec_send_packet` / `avcodec_receive_frame` → `sws_scale` 转 `640×480 RGB24` → `emit(meta)`。单次拉流供后续节点使用；如开启 `--record-clip <path> --duration <sec>`，在解码前把原始视频 `AVPacket` remux 写入目标 MP4；`RemuxRecorder` 只创建视频输出流，因此录片仅保留视频轨，避免重编码 CPU 开销。

本地文件 `loop == true` 且遇到 `AVERROR_EOF` 时执行 `av_seek_frame` + `avcodec_flush_buffers`；按 `target_fps` 用 `steady_clock` 节流。网络流断线后间隔 2 秒重连。切源失败必须保留原源，返回接口规定的 400 错误；不能先销毁旧源再报告失败。

**离线测试，目标 3～5 个用例。当前 `tests/test_capture.cpp` 已有 2 个采集用例（本地解码/EOF 循环、非法源保留原源），其余采集用例由 Issue #51 补齐；`tests/test_api.cpp` 已覆盖健康探针、切源与快速启停回环。**

1. 极小本地测试视频能读出帧，RGB24 尺寸和元数据符合契约。
2. EOF 后循环重置，刷新解码器，能继续读帧。
3. 非法路径／源能安全报错，不崩溃。
4. 回环调用 `/health` 可读取控制面存活信息。
5. 回环调用 `/source/switch` 验证切源成功和失败保留原源，可在同一用例包含两次断言。

**阶段 A 真机验收：**

在联想宿主机完成 ARM64 交叉编译；经单独授权部署到魔百盒后，成功拉取手机 IP Webcam 流，按配置保存 `.jpg`，录制含静止与人物运动的 `assets/demo_clip.mp4`，再切换为该视频稳定循环。本机 x86_64 构建与离线测试已完成；2026-11-24 的首次真机验收因目标机无外网且缺少 aarch64 sysroot，改用盒上离线原生构建，并以本地 `assets/demo_clip_30s.mp4` 循环完成部署、控制面、检测、OSD、MJPEG 与优雅退出验收。手机 IP Webcam 的 `/shot.jpg` 与 `/video` 均超时，因此 ARM64 交叉编译、手机流与设备端截帧／录片仍待补。编译通过、部署完成、现场通路验收分别记录，不能混写为“上线可用”。

### 4.2 `v0.2-discern` — 帧差漏斗与 NCNN 端侧检测

前置条件：阶段 A 已验收，本地循环视频可用。

**全部交付件：**

- `src/detector/motion_node.hpp` / `.cpp`。
- `src/detector/ncnn_detector_node.hpp` / `.cpp`。
- `src/detector/model_selection.hpp` / `.cpp`：按 `quantization` 与设备资源选择 fp32/fp16/int8 候选模型。
- `models/README.md`：YOLO-Fastest-v2 / NanoDet-Plus `.param`、`.bin` 获取与放置路径；模型权重不入仓库，不由测试自动下载。
- `ApiServer` 已补充 `PATCH /api/v1/config`，支持原子配置快照热更新；字段、响应与限制见接口契约。
- `tests/test_detector.cpp`，3～5 个纯离线用例。

**内部架构：**

`MotionNode::process(meta)` 步长采样 RGB24 为 `64×48` 灰度图，大小 3072 字节，与上一帧计算 MAD。低于 `motion_mad_threshold` 时写 `has_motion = false`，静态帧继续透传，但跳过检测推理。

`NcnnDetectorNode::process(meta)` 仅在 `has_motion == true` 时调用 `ncnn::Net`。加载模型 `.param` / `.bin`，开启 `use_fp16_arithmetic = true`、ARM NEON，设置 `num_threads = 2`，输入 `320×320`；通过 `from_pixels_resize` 预处理，按 `target_labels`、`conf_threshold` 过滤并执行标准 NMS，写入 `targets` 和 `infer_ms`。

模型加载前由 `select_model()` 按 `detector.quantization` 选型：`auto` 在设备支持 int8 且内存充足时优先 int8，其次 fp16，否则基础模型；显式档位缺失候选时回退基础模型。fp16 档启用 fp16 算术与存储，fp32/int8 档关闭 fp16 算术选项，int8 由 ncnn 的 int8 推理开关控制；候选文件加载失败时再回退基础模型，并通过 `effective_quantization()` 报告实际档位。

热更新覆盖 `detector.motion_mad_threshold`、`detector.conf_threshold`、`llm.enabled`、`llm.cooldown_sec` 和 `storage.enabled`；其中存储开关在 v0.3-nous 接入投递步骤，不把其他配置隐式开放为热更新接口。内部调用信任既定 struct / interface，外部输入按接口契约处理，不堆砌重复防御。

**离线测试（规划 4 个核心用例，另含检测后处理与量化选型用例）：**

1. 静止图被 MAD 漏斗拦截，不触发检测推理。
2. 变化图超过阈值，放行检测。
3. 空帧安全透传。
4. 检测结果按目标类别和阈值过滤，NMS 输出符合预期。
5. 量化选型：显式档位命中、auto 按内存与指令集选择、候选缺失回退基础模型。

基于本地循环视频记录帧差跳过率、检测 FPS、推理耗时与 RSS，作为本阶段实际结果；没有实测时不填造指标。

### 4.3 `v0.3-nous` — 瞬时事件、异步推理与 PC 云存储

前置条件：本地循环视频与端侧检测链路稳定。

**全部交付件：**

- [start_pc_services.sh](../scripts/start_pc_services.sh) 已实现：从环境变量读取模型路径与端口，后台启动 `llama-server :8080` 与 `cloud-storage :8090`，记录 PID、写临时日志，并循环探活两个 `/health`。任一启动或探活失败返回非零并提示停止命令；真实模型联调待联想 PC 执行。
- `src/transport/event_tracker_node.hpp` / `.cpp`：仅当前活跃事件的瞬时聚合状态和触发节流。（已实现并接入运行入口）
- `src/transport/jpeg_base64.hpp` / `.cpp`：干净 JPEG 一次内存编码与 Base64 复用。（已实现）
- `src/transport/llm_selection.hpp` / `.cpp`：按配置档位、可用模型列表、显存与读取超时选择 LLM 模型别名，不可用时回退基础 `llm.model`；`tests/test_llm_selection.cpp` 覆盖 4 个离线用例。（已实现）
- `src/transport/genai_worker.hpp` / `.cpp`：单后台线程、队列 1，顺序执行可选 LLM 和可选 Storage HTTP。（已实现并接入运行入口）
- 更新边缘健康接口，分别报告 LLM `:8080` 与 Storage `:8090` 状态；在配置热更新中接入 `storage.enabled`。边缘 `:8081` 不提供历史事件列表。
- `cloud-storage/server.py` 已实现为联想 PC 独立 FastAPI + SQLite 进程，监听 `:8090`，与 `llama-server :8080` 和 C++ ARM64 构建解耦。
- `cloud-storage/README.md` 已提供实际启动／运行说明，使用 `requirements.txt` 中 `fastapi`、`uvicorn`、`pydantic` 和 `schema.sql` 的表／索引契约，并有离线 pytest 覆盖。
- PC 实现健康、事件和图片接口，并接收和查询 `GET`／`POST /api/v1/logs`。接口语义和日志分日方式以 [api](api.md) / [OpenAPI](openapi.yaml) 为准。
- `tests/test_transport.cpp`，3～5 个离线用例；后续 PC 服务另用临时目录和临时 SQLite 验证存储／分页，不依赖真实模型、私人影像或真实运行数据库。当前已实现 4 个 `GenAiWorker`／JPEG 用例与 3 个 `EventTrackerNode` 用例，覆盖两开关关闭、仅 LLM、LLM 失败后空摘要投递、队列丢旧释放，以及单事件单任务、冷却拦截并持续更新、宽限期后释放并允许新事件。

**内部架构：**

`EventTrackerNode::process(meta)` 参考 Frigate Event Lifecycle，按事件触发异步处理，不逐帧请求 LLM 或存储服务：

1. 检测到目标、没有活跃事件且满足触发冷却条件，生成事件 ID，保留当前事件时间／最佳目标等瞬时聚合状态，把触发时代表帧异步交给 `GenAiWorker`。触发冷却沿用配置 `llm.cooldown_sec`，即使关闭 LLM 仍约束事件任务生成，不另造隐式重试策略。
2. 连续出现目标只更新当前活跃时间和最佳目标，不重复投递。异步任务携带独立的触发时元数据快照和干净帧引用，不能与活跃状态或 OSD 并发改写同一缓冲。
3. 连续超过 `event.end_grace_sec`（默认 5 秒）无目标，结束并释放当前活跃事件状态。new / update / end 是瞬时聚合过程；不写边缘历史，不要求 PC 对该生命周期保存额外开始／结束字段或持续回填。

`GenAiWorker` 使用单后台线程和容量 1 的 `BoundedQueue`，只保留最新待处理任务。任务处理顺序固定：

1. 两个开关都关闭时直接结束任务，跳过 JPEG 编码和 HTTP。
2. 否则从未叠加 OSD 的原始帧，用 `stbi_write_jpg_to_func` 按 JPEG 质量 70 仅编码一次，再得到一份 Base64；供后续两个 HTTP 步骤复用，不为 LLM／存储各编码一遍。
3. `llm.enabled == true` 时，先请求 `llm.endpoint`，连接／读取超时默认 3／25 秒。成功解析描述和耗时；关闭、超时或失败时 `llm_summary` 为 `""`，仍进入已开启的存储步骤。
4. `storage.enabled == true` 时，向 `storage.endpoint`（默认 `http://192.168.10.10:8090/api/v1/events`）提交同一份 JPEG Base64、事件／帧号、毫秒时间、最佳目标、目标数组和可选描述，默认超时 `storage.timeout_sec = 5` 秒。存储失败只记必要日志，释放任务，不重试、不留本地图片或失败任务队列。

所有 HTTP 等待发生在 Nous 单工作线程；采集、检测与实时输出不等待请求结果。视频链路输入队列容量 2、Nous 待处理容量 1 均满则丢旧，处理完成或丢弃后及时释放图像引用。魔百盒 RSS 硬约束为 **<150MB**，后续以实测验收；本轮不声称已达标。

严格消费联想 PC 的原生 `GET /health` 和 `POST /v1/chat/completions`，不修改上游服务端。图像为 `data:image/jpeg;base64,...`，请求遵循 OpenAI Vision 消息结构，使用配置中的模型与中文提示词；启动规格的请求参数为 `max_tokens: 128`、`temperature: 0.2`。请求的 `model` 先经 `llm_selection` 选型：启动时用 `GET /v1/models` 探测可用别名（失败视为未限制），`auto` 按 q8、q5、q4 顺序选择可用、显存足够且不超过 `read_timeout_sec` 的别名（0 表示显存未知），显式档位不可用或显存不足时回退基础 `llm.model`。解析 `choices[0].message.content`，精确请求格式见 [api.md](api.md)。

**PC 存储／查询内部契约：**

FastAPI `POST /api/v1/events` 解码干净 JPEG，把图片写入 `cloud-storage/storage/images/YYYY-MM-DD/<event_id>.jpg`，并将元数据写入 `cloud-storage/storage/events.db`，按 API 契约处理异常设备时间，以生效时间生成图片日期目录并写入 SQLite，成功返回 `201 Created`。SQLite 表和时间索引以 [schema.sql](../cloud-storage/schema.sql) 为唯一结构依据；`targets` 序列化为 `targets_json`，持久化 `image_rel_path`，查询响应提供 `image_url`。日期目录时区、分页排序、重复 ID 与错误响应以 [api.md](api.md) / [OpenAPI](openapi.yaml) 为准。

`GET /api/v1/events?limit=20&offset=0` 分页查询 PC 已接收的历史；`GET /api/v1/images/{date}/{filename}` 返回对应干净图片；`GET /health` 返回服务／数据库状态。运行数据目录整体忽略，不入库；云端失败或网络离线可能导致事件丢失，不承诺离线补发或必达。截图、数据库和历史 API 仅归 PC 存储业务，不借用边缘 C++ 模块实现。

**边缘 transport 离线测试，规划 5 个用例：**

1. 连续多帧含目标只生成一个活跃事件和一个异步任务；宽限期结束后瞬时状态被释放，不产生历史留存。
2. 冷却期内新任务被拦截；队列满时丢旧，并释放旧帧引用。
3. 用 Mock 验证干净 JPEG 只编码一次、LLM 先于 Storage、两个开关组合符合契约；请求 JSON 与返回解析正确。
4. LLM 连接／读取超时或失败时，仍以空描述向已开启的 Storage 投递，不阻塞视频流水线。
5. Storage 超时／失败不崩溃、不重试、不写本地缓存，不造成任务无界积压。

**PC 服务离线验收，已实现 5 个用例（`cloud-storage/test_server.py`）：**

1. 临时 SQLite 依据 schema 初始化后健康检查成功。
2. 投递极小干净 JPEG 后得到 201，图片相对路径和元数据对应；读取图片能还原输入内容。
3. 插入多条记录后分页、排序、`image_url` 和查询字段符合接口契约。
4. 无效请求／缺失图片／重复事件 ID 按接口约定返回，未产生越界路径或不一致记录；使用临时目录，不操作真实数据库。
5. 用临时目录验证 Fluent Bit JSON 数组写入日期日志，POST 返回 204，GET 只返回所选日期的末尾 N 行。

后续真实联调分别记录 LLM 与 Storage 可达性、LLM 延迟、成功持久化事件／图片、历史查询和上游关闭时的视频稳定性。离线验证不替代真实服务联调；运行数据不得推送公共仓库。

### 4.4 `v1.0-vox` — OSD、双模推流与守护交付

前置条件：本地循环视频、检测、瞬时事件与异步可选推理／存储链路可用；实时输出不依赖历史服务成功。

**全部交付件：**

- `src/streamer/osd_annotator.hpp` / `.cpp`。
- `src/streamer/stream_out_node.hpp` / `.cpp`。
- `src/streamer/rtsp_pusher.hpp` / `.cpp`。
- `src/main.cpp`：声明式 `BaseNode::attach_next()` 组装与 `SIGINT` / `SIGTERM` 优雅退出（已实现，Issue #88）。
- 在目标机验证已有 `deploy/edge-vision.service`、`deploy/journald-edge.conf` 和 `deploy/fluent-bit.conf`，并已实现 [deploy_to_box.sh](../scripts/deploy_to_box.sh)，统一对时、打印环境、传包、重启和验活，最多 50 行。脚本与模板已就绪；`edge-vision.service` 已于 2026-11-24 在魔百盒安装运行，journald／fluent-bit 模板仍未安装。
- `ApiServer` 补充 `GET /api/v1/snapshot.jpg` 和 `GET /api/v1/stream`。
- `tests/test_streamer.cpp`，3～5 个离线用例。
- 完善 `README.md`：架构图、部署步骤、实测性能指标表；发布／部署／现场验收结果分别记录。

**内部架构：**

`OsdAnnotator` 直接处理 RGB24，绘制 2px 矩形框与内置 `8×8` ASCII 点阵标签，例如 `person 0.87`；不依赖 OpenCV 或外部字体。归一化框转像素坐标后须裁剪到画面边界。

`StreamOutNode` 默认采用 `mjpeg`：只在存在活跃 `/api/v1/stream` 连接或收到 `/api/v1/snapshot.jpg` 请求时调用 `stbi_write_jpg_to_func`，广播带 OSD 的图像；无人观看且无快照请求时跳过 JPEG 编码。MJPEG 媒体类型为 `multipart/x-mixed-replace; boundary=frame`；快照无首帧时返回 503。

可选 `rtsp` 模式由 `RtspPusher` 启动 FFmpeg 管道，以 `-preset ultrafast -tune zerolatency -b:v 500k` 推送外部 `bluenviron/mediamtx`；不在边缘程序里实现 RTSP 服务端。选择模式、地址和端口从统一配置读取。Issue #98 已把推流器按 `config.streamer` 注入 `StreamOutNode`：`rtsp` 时逐帧在独立缓冲画 OSD 后以 RGB24 推送，`mjpeg` 时不启动子进程；推流器启动失败只告警，MJPEG 端点与流水线继续运行。启动和停止由 `src/main.cpp` 接入现有优雅退出，先停节点再回收编码进程。MJPEG 与快照已在魔百盒验收；真机 MediaMTX 联调与画质、延迟验收仍待执行。

**离线测试（已实现，使用 FakeProcessSink，不启动真实 ffmpeg 或网络）：**

1. 越界检测框被裁剪，绘制不越界。
2. 无客户端且无快照请求时跳过 JPEG 编码；mjpeg 模式不启动推流进程。
3. 注入推流器后 OSD 帧以 RGB24 推送，独立缓冲不修改共享帧；推流启动失败时按需 MJPEG 仍可编码。
4. 停止时关闭输入并回收推流进程，重复停止幂等；有界队列满时丢帧。

后续经授权在魔百盒部署并验收 systemd 启停、信号退出、MJPEG／选定 RTSP 入口与 RSS <150MB；PC 两个进程不可达时，实时输出仍须正常。仅有二进制或 service 文件不代表服务已部署。

## 5. 统一验收与接力记录

- 每个核心模块仅保留 3～5 个 GoogleTest 用例；全部 CTest 测试应在 5 秒内完成。不为 getter / setter、纯数据结构和日志打印编写测试。
- 单元测试只使用 `tests/fixtures/` 的极小素材、Mock 或回环桩；不自动下载权重，不等候真实摄像头或 LLM。真实硬件验收单独执行、单独记证据。
- 常规 C++ 开发只按 [AGENTS.md](../AGENTS.md) 执行必要构建与 CTest，PC 服务独立验证，不把 Python 服务并入 ARM64 编译；不新增环境自检、摘要校验或重复 wrapper。每个 Issue 只执行其获准范围的构建与测试，并记录实际运行结果。
- 问题按 AGENTS 第 4 节分流。环境小坑记入 Discussions，已解决的补录 [踩坑记录](troubleshooting.md)。代码缺陷或阻塞问题记入 `type:bug,agent-log` Issue。
- 后续交接至少记录：任务／Issue、获准范围、分支与完整提交 SHA、已改文件、实际运行的验证及结果、未完成项和下一步。状态明确区分“已修改／已验证／已构建／已部署／线上已验收”。
- 里程碑完成后更新本文件的实际状态与对应证据，接口变化同步 api / openapi，配置变化同步 config；不要把计划测试和预期指标写成已经发生的结果。
