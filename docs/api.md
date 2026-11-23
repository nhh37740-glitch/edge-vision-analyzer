# Edge Vision Analyzer — API 契约

状态：有效设计契约；全部端点待实现。维护责任：项目维护 Agent。更新日期：2026-11-23（北京时间）。

本轮仅初始化文档，不代表 API 已运行或完成验收。机器可读契约见 [openapi.yaml](openapi.yaml)，架构见 [architecture.md](architecture.md)，阶段交付与离线验收见 [roadmap.md](roadmap.md)，开发与执行边界遵守 [AGENTS.md](../AGENTS.md)。所有默认配置来自 [config/config.json](../config/config.json)。

## 1. 边缘端约定

魔百盒进程计划监听 `0.0.0.0:8081`；调用方使用设备实际 IP，静态组网示例基础地址为 `http://192.168.10.11:8081`。资源前缀为 `/api/v1`。普通请求和响应使用 `application/json`，快照与流使用下表指定媒体类型。端口与视频源可由启动配置提供。

| 方法 | 路径 | 计划里程碑 | 契约 |
| --- | --- | --- | --- |
| GET | `/api/v1/health` | `v0.1-oculus` | 存活、版本、运行时长、各节点线程状态、上游可达性 |
| GET | `/api/v1/stats` | `v0.1-oculus` | 采集/检测 FPS、耗时、丢帧、RSS |
| GET | `/api/v1/config` | `v0.1-oculus` | 生效配置，`llm.api_key` 始终返回 `***` |
| PATCH | `/api/v1/config` | `v0.2-discern` | 原子热更新四个允许字段 |
| POST | `/api/v1/source/switch` | `v0.1-oculus` | 切换手机流/本地文件；失败保留原源 |
| GET | `/api/v1/events` | `v0.3-nous` | 查询容量 100 的内存环形事件表 |
| GET | `/api/v1/snapshot.jpg` | `v1.0-vox` | 最新 OSD JPEG；无首帧返回 503 |
| GET | `/api/v1/stream` | `v1.0-vox` | `multipart/x-mixed-replace; boundary=frame` MJPEG |

共 7 个资源路径、8 个操作；OpenAPI 仅定义上述边缘端接口。联想 PC 的推理接口是外部依赖，见第 7 节。

HTTP 400 / 404 / 500 / 503 使用同一错误体：

```json
{
  "error": {
    "code": "INVALID_PARAMETER",
    "message": "motion_mad_threshold must be between 0.0 and 255.0"
  }
}
```

400 表示非法字段、范围或切源初始化失败；404 表示资源/路径不存在；500 表示内部错误；503 表示服务或画面暂未就绪。错误信息不包含密钥。JSON 结构、字段范围和错误响应定义统一维护在 OpenAPI 中。

## 2. 健康与统计

`GET /api/v1/health` 成功示例：

```json
{
  "status": "ok",
  "version": "v0.1-oculus",
  "uptime_sec": 125.4,
  "nodes": [
    {"name": "CaptureNode", "state": "running"},
    {"name": "NcnnDetectorNode", "state": "not_enabled"}
  ],
  "llm": {
    "status": "reachable",
    "endpoint": "http://192.168.10.10:8080/health"
  }
}
```

节点状态为 `starting/running/stopping/stopped/failed/not_enabled`。上游状态为 `disabled/reachable/unreachable/unknown`。边缘进程存活但上游不可达时，返回 200、`status: degraded`，不把上游故障当作边缘进程死亡；上游检查不触发真实多模态生成，不能让探活等待推理。尚未在当前里程碑实现的节点按实际状态报告，不虚构线程已经运行。

`GET /api/v1/stats` 成功示例：

```json
{
  "capture_fps": 14.8,
  "detect_fps": 4.2,
  "motion_skip_ratio": 0.72,
  "avg_infer_ms": 84.5,
  "avg_llm_latency_ms": 6210.0,
  "dropped_frames": 12,
  "rss_memory_mb": 142.6
}
```

FPS、耗时和 RSS 均为非负数，`motion_skip_ratio` 为 `[0,1]`，`dropped_frames` 为累计非负整数。未启用或尚无样本的阶段指标返回 0。RSS 约束为不超过 300MB，实际超限仍须如实报告测量值。

## 3. 配置读取与热更新

`GET /api/v1/config` 返回当前生效配置，保留 [配置模板](../config/config.json) 的全部字段：

| 配置块 | 字段 |
| --- | --- |
| `input` | `source`, `loop`, `width`, `height`, `target_fps`, `dump_interval_sec` |
| `detector` | `motion_mad_threshold`, `model_param`, `model_bin`, `input_size`, `conf_threshold`, `num_threads`, `target_labels` |
| `event` | `end_grace_sec`, `max_events_in_memory` |
| `llm` | `enabled`, `endpoint`, `api_key`, `model`, `prompt`, `cooldown_sec`, `connect_timeout_sec`, `read_timeout_sec`, `jpeg_quality` |
| `streamer` | `mode`, `http_port`, `rtsp_url` |

其中 `llm.api_key` 无论原配置是什么，响应都为 `***`。OpenAPI 的 `Config/PublicLlmConfig` 是公开响应视图；磁盘示例配置里的 `local-no-key` 只是无真实凭据的占位值。

`PATCH /api/v1/config` 仅允许以下四个叶子字段，保持嵌套结构：

```json
{
  "detector": {"motion_mad_threshold": 8.0, "conf_threshold": 0.5},
  "llm": {"enabled": true, "cooldown_sec": 15}
}
```

- `detector.motion_mad_threshold`：`0.0..255.0`。
- `detector.conf_threshold`：`0.0..1.0`。
- `llm.enabled`：布尔值。
- `llm.cooldown_sec`：非负秒数。

可仅提交其中一个字段。拒绝空对象、未知字段、错误类型、范围错误及任何不在以上清单的字段，返回 400。先验证整份请求，再无锁发布新的不可变配置快照；任一字段失败则全部不生效。成功返回 200 和完整脱敏生效配置。不持久化修改磁盘文件；视频源必须走切源接口，不能通过 PATCH 更改端口、模型、线程数或密钥。

## 4. 视频源切换

`POST /api/v1/source/switch` 请求：

```json
{"source": "assets/demo_clip.mp4", "loop": true}
```

成功返回 200：

```json
{"status": "switched", "source": "assets/demo_clip.mp4", "loop": true}
```

切回手机时使用 `{"source":"http://192.168.10.12:8080/video","loop":false}`；RTSP 可使用实际设备的 RTSP URL。两个字段均必填，`source` 必须为非空字符串。新源成功打开并完成解码初始化后才提交切换；失败返回 400 并继续使用原视频源，不能先停止旧源再留下空档。源切换成功后 GET 配置反映新的 `input.source/input.loop`。本地路径相对进程工作目录，交付时应以仓库/部署资源根目录运行。

## 5. 事件查询

`GET /api/v1/events?limit=20`：`limit` 默认 20，范围 1..100，非法值返回 400。返回最近事件优先；`total_in_buffer` 是缓冲区全部条数，`events` 最多返回 `limit` 条。环形表容量为 100，重启后不保留。

```json
{
  "total_in_buffer": 1,
  "events": [
    {
      "event_id": "evt_1795412000_1",
      "start_ts_ms": 1795412000123,
      "end_ts_ms": 1795412008450,
      "status": "ended",
      "top_label": "person",
      "top_score": 0.87,
      "boxes": [
        {"label": "person", "score": 0.87, "x": 0.21, "y": 0.15, "w": 0.30, "h": 0.68}
      ],
      "llm_summary": "一名男子正经过客厅走向门口。",
      "llm_latency_ms": 5840.2
    }
  ]
}
```

时间为 Unix 毫秒整数。生命周期动作为 `new/update/end`，资源 `status` 为 `active/ended`；活跃事件的 `end_ts_ms` 随最近目标出现时间更新。框坐标 `x/y/w/h` 相对画面归一化至 `[0,1]`，`x/y` 是左上角；置信度为 `[0,1]`，`class_id` 可选。LLM 尚未完成、关闭或失败时，摘要允许为空字符串、耗时为 0。语义结果异步回填，不阻塞事件列表、采集和推流。

## 6. 快照与 MJPEG

`GET /api/v1/snapshot.jpg` 返回 `200 Content-Type: image/jpeg` 和带 OSD 的 JPEG 二进制；尚无首帧时返回 503 和统一 JSON 错误体。

`GET /api/v1/stream` 返回长连接，响应类型为 `multipart/x-mixed-replace; boundary=frame`。每个部分以 `--frame` 分隔，包含 `Content-Type: image/jpeg` 和单帧 JPEG 二进制。发送 HTTP 流头前无可用帧时可返回 503/JSON；开始流后不得插入 JSON 错误体，断线由客户端重连。慢客户端不能造成无界缓存。

只有存在活跃流客户端或收到快照请求时才进行 JPEG 编码，无人观看时不持续编码。可选 RTSP 通过外部 MediaMTX 中继发布，不新增手写 RTSP REST 资源。

## 7. 上游 llama-server（外部依赖）

联想 PC 的 `llama-server` 计划监听 `0.0.0.0:8080`，静态组网示例为 `192.168.10.10`。边缘 `GenAiWorker` 消费原生接口，不修改上游服务端代码：

- `GET http://192.168.10.10:8080/health`：200 表示上游可用。
- `POST http://192.168.10.10:8080/v1/chat/completions`：OpenAI 兼容多模态请求。

```json
{
  "model": "vlm-local",
  "max_tokens": 128,
  "temperature": 0.2,
  "messages": [
    {
      "role": "user",
      "content": [
        {"type": "text", "text": "用一句简短的中文描述画面中人物正在做什么。"},
        {"type": "image_url", "image_url": {"url": "data:image/jpeg;base64,<BASE64_JPEG>"}}
      ]
    }
  ]
}
```

成功结果取 `choices[0].message.content`，关联原 `event_id` 回填事件表。JPEG 通过 `stbi_write_jpg_to_func` 在内存中编码，默认质量 70；连接超时 3 秒、读取超时 25 秒。单后台线程、待分析队列容量 1，仅保留最新待分析事件；事件触发与冷却机制见架构和路线图。上游错误只影响异步摘要，不能阻塞视频流水线。配置实际凭据时由客户端发送相应认证信息，凭据不得在边缘配置响应、日志或 Issue 中回显。

本节接口不属于本仓库边缘端 OpenAPI 的 paths；后续与真实上游版本联调时核对其实际多模态支持，不能以此文档宣称已部署或已验证。
