# Edge Vision Analyzer — 双服务 API 契约

状态：有效设计契约，全部端点待实现。维护责任：项目维护 Agent。更新日期：2026-11-23（北京时间）。依据：PATCH-001、PATCH-002、PATCH-003；存储与历史查询以 PATCH-003 为准。

本轮仅初始化文档，不代表 API 已运行或完成验收。机器可读契约见 [openapi.yaml](openapi.yaml)，系统边界见 [architecture.md](architecture.md)，交付与离线验收见 [roadmap.md](roadmap.md)，开发和执行边界遵守 [AGENTS.md](../AGENTS.md)。默认配置见 [config/config.json](../config/config.json)，PC 数据表契约见 [schema.sql](../cloud-storage/schema.sql)。

## 1. 两个服务的地址与职责

| 服务 | 示例地址 | 唯一职责 |
| --- | --- | --- |
| 魔百盒边缘端 | `http://192.168.10.11:8081` | 实时控制、状态与带 OSD 的实时画面 |
| 联想 PC cloud-storage | `http://192.168.10.10:8090` | 留存干净 JPEG、SQLite 结构化事件与历史查询 |
| 联想 PC llama-server（外部依赖） | `http://192.168.10.10:8080` | 多模态语义推理，不承担留存与历史查询 |

边缘进程计划监听 `0.0.0.0:8081`，云存储进程计划监听 `0.0.0.0:8090`。调用时使用实际设备地址。OpenAPI 的每个路径分别设置 `servers`，避免把不同服务当作可互换的服务器列表。

边缘端共 6 个资源路径、7 个操作：

| 方法 | 路径 | 计划里程碑 | 契约 |
| --- | --- | --- | --- |
| GET | `/api/v1/health` | `v0.1-oculus` | 存活、版本、时长、节点线程及 LLM/Storage 连通状态 |
| GET | `/api/v1/stats` | `v0.1-oculus` | FPS、跳过率、耗时、丢帧及 RSS |
| GET | `/api/v1/config` | `v0.1-oculus` | 生效配置，`llm.api_key` 始终为 `***` |
| PATCH | `/api/v1/config` | `v0.2-discern`，存储开关于 `v0.3-nous` 接入 | 原子热更新检测/LLM/Storage 五项字段 |
| POST | `/api/v1/source/switch` | `v0.1-oculus` | 切换手机流/本地文件；失败保留原源 |
| GET | `/api/v1/snapshot.jpg` | `v1.0-vox` | 实时 OSD JPEG；无首帧返回 503 |
| GET | `/api/v1/stream` | `v1.0-vox` | 实时 OSD MJPEG |

云存储端共 3 个资源路径、4 个操作，均属于 `v0.3-nous`：

| 方法 | 路径 | 契约 |
| --- | --- | --- |
| GET | `/health` | 数据库连接健康探针 |
| POST | `/api/v1/events` | 接收干净抓拍与结构化结果，成功返回 201 |
| GET | `/api/v1/events` | 以 `limit/offset` 分页查询历史事件 |
| GET | `/api/v1/images/{date}/{filename}` | 返回对应历史事件的干净 JPEG |

PC 提供历史 `GET /api/v1/events`。边缘只保留当前活动事件的聚合状态，历史图片与结构化数据由 PC 存储。

JSON 请求与响应使用 `application/json`。HTTP 400 / 404 / 500 / 503 采用同一错误体：

```json
{
  "error": {
    "code": "INVALID_PARAMETER",
    "message": "motion_mad_threshold must be between 0.0 and 255.0"
  }
}
```

400 表示字段、参数、源初始化或图像输入非法；404 表示资源不存在；500 表示内部错误，包括文件/SQLite 写入失败；503 表示边缘画面/节点或云数据库未就绪。错误信息不得暴露磁盘路径或凭据。JSON 字段与范围维护在 OpenAPI 中。

## 2. 边缘健康与统计

`GET :8081/api/v1/health` 成功示例：

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
  },
  "storage": {
    "status": "reachable",
    "endpoint": "http://192.168.10.10:8090/health"
  }
}
```

节点状态为 `starting/running/stopping/stopped/failed/not_enabled`。两个上游分别报告 `disabled/reachable/unreachable/unknown`；尚未接入的阶段如实报告未启用或未知。边缘进程存活而已启用的上游不可达时，返回 200、`status: degraded`。探活只检查连通状态，不能触发真实多模态生成或存储写入，也不能让边缘存活探针等待推理。

`GET :8081/api/v1/stats` 成功示例：

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

保留原统计中无冲突的平均 LLM 耗时和累计丢帧指标。未启用或尚无样本的阶段指标返回 0。无状态边缘节点 RSS 目标严格 **小于 150MB**；实际超限仍须如实报告测量值。

## 3. 边缘配置读取与热更新

`GET :8081/api/v1/config` 返回当前生效配置，覆盖 [config.json](../config/config.json) 的全部字段：

| 配置块 | 字段 |
| --- | --- |
| `input` | `source`, `loop`, `width`, `height`, `target_fps`, `dump_interval_sec` |
| `detector` | `motion_mad_threshold`, `model_param`, `model_bin`, `input_size`, `conf_threshold`, `num_threads`, `target_labels` |
| `event` | `end_grace_sec` |
| `llm` | `enabled`, `endpoint`, `api_key`, `model`, `prompt`, `cooldown_sec`, `connect_timeout_sec`, `read_timeout_sec`, `jpeg_quality` |
| `storage` | `enabled`, `endpoint`, `timeout_sec` |
| `streamer` | `mode`, `http_port`, `rtsp_url` |

`event.max_events_in_memory` 已移除。`llm.api_key` 无论原配置是什么，响应都为 `***`。OpenAPI `Config/PublicLlmConfig` 是公开响应视图；磁盘示例中的 `local-no-key` 仅为占位值。

`PATCH :8081/api/v1/config` 使用嵌套结构：

```json
{
  "detector": {"motion_mad_threshold": 8.0, "conf_threshold": 0.5},
  "llm": {"enabled": true, "cooldown_sec": 15},
  "storage": {"enabled": true}
}
```

仅允许五个叶子字段：`detector.motion_mad_threshold`（0..255）、`detector.conf_threshold`（0..1）、`llm.enabled`（布尔）、`llm.cooldown_sec`（非负秒数）、`storage.enabled`（布尔）。原四项在 `v0.2-discern` 实现，存储开关于 `v0.3-nous` 接入。可只提交一个字段；拒绝空对象、未知字段、错误类型及范围错误，返回 400。

先验证整份请求，再无锁发布不可变配置快照；任一字段失败则全部不变。成功返回 200 和完整脱敏生效配置，不持久化到磁盘。视频源走专用切源接口；模型、线程数、端口、密钥以及 `storage.endpoint/storage.timeout_sec` 仅静态配置，不能热更。

```json
{
  "storage": {
    "enabled": true,
    "endpoint": "http://192.168.10.10:8090/api/v1/events",
    "timeout_sec": 5
  }
}
```

## 4. 边缘视频源切换与实时画面

`POST :8081/api/v1/source/switch` 请求：

```json
{"source": "assets/demo_clip.mp4", "loop": true}
```

成功返回 200：

```json
{"status": "switched", "source": "assets/demo_clip.mp4", "loop": true}
```

切回手机可使用 `{"source":"http://192.168.10.12:8080/video","loop":false}`，也可使用实际 RTSP 地址。两个字段必填，`source` 为非空字符串。新源打开并完成解码初始化后才提交切换；失败返回 400，继续原视频源。成功后 GET 配置反映新的 `input.source/input.loop`。本地路径相对进程工作目录，交付时以仓库/部署资源根目录运行。

`GET :8081/api/v1/snapshot.jpg` 返回最新实时 OSD JPEG，媒体类型 `image/jpeg`；尚无首帧返回 503/JSON。它是实时展示快照，不是历史干净抓拍。

`GET :8081/api/v1/stream` 返回长连接，媒体类型为 `multipart/x-mixed-replace; boundary=frame`，每个部分以 `--frame` 分隔并包含 `Content-Type: image/jpeg`。发送流头前无可用帧可返回 503/JSON；开始流后不能插入 JSON 错误体，断线由客户端重连。慢客户端不得造成无界缓存。仅有活跃客户端或收到快照请求时进行 JPEG 编码。可选 RTSP 使用外部 MediaMTX 中继。

## 5. PC 云存储 API

### 5.1 健康检查

`GET :8090/health` 成功返回 200：

```json
{"status": "ok", "db": "connected"}
```

数据库未就绪时返回 503 和统一 Error，不报告虚假的 `db: connected`。

### 5.2 保存事件与干净抓拍

`POST :8090/api/v1/events` 请求仅使用以下字段：

```json
{
  "event_id": "evt_1795412000_1",
  "frame_id": 88210,
  "timestamp_ms": 1795412000123,
  "top_label": "person",
  "top_score": 0.87,
  "targets": [
    {"label": "person", "score": 0.87, "x": 0.21, "y": 0.15, "w": 0.30, "h": 0.68}
  ],
  "llm_summary": "一名男子正经过客厅走向门口。",
  "llm_latency_ms": 5840.2,
  "image_base64": "<CLEAN_JPEG_BASE64>"
}
```

全部字段必填；LLM 关闭、超时或失败时 `llm_summary` 为空字符串，不影响后续存储。`image_base64` 是同一份未经 OSD 绘制的干净 JPEG 的 Base64 原文，不带 Data URI 前缀。框坐标归一化至 `[0,1]`，置信度为 `[0,1]`，目标框内 `class_id` 可选。

服务端从 `timestamp_ms`（Unix 毫秒）计算 **UTC 日期**。该示例对应 `2026-11-23`，图片写入 `cloud-storage/storage/images/2026-11-23/evt_1795412000_1.jpg`，SQLite 写入 `cloud-storage/storage/events.db`。`image_rel_path` 以 `storage/` 为根，存为 `images/2026-11-23/evt_1795412000_1.jpg`；`targets` 序列化为 `targets_json`。完整列定义与索引见 [schema.sql](../cloud-storage/schema.sql)，不在此重复维护建表 SQL。

图片和数据库记录均写入成功后返回 **201 Created**：

```json
{
  "status": "created",
  "event_id": "evt_1795412000_1",
  "image_url": "/api/v1/images/2026-11-23/evt_1795412000_1.jpg"
}
```

`event_id` 仅允许字母、数字、下划线和连字符。文件名由服务端按 `<event_id>.jpg` 生成，不接收调用方文件路径。无效 Base64/JPEG/字段返回 400；重复 `event_id` 返回 400、`error.code: EVENT_EXISTS`，不覆盖原留存；文件或数据库写入失败返回 500。错误响应不暴露物理路径。

### 5.3 分页查询

`GET :8090/api/v1/events?limit=20&offset=0`：`limit` 默认 20、范围 1..100；`offset` 默认 0、为非负整数。非法值返回 400。按 `timestamp_ms DESC` 排序，同毫秒以 `event_id DESC` 稳定排序。

```json
{
  "total": 1,
  "limit": 20,
  "offset": 0,
  "events": [
    {
      "event_id": "evt_1795412000_1",
      "frame_id": 88210,
      "timestamp_ms": 1795412000123,
      "top_label": "person",
      "top_score": 0.87,
      "targets": [
        {"label": "person", "score": 0.87, "x": 0.21, "y": 0.15, "w": 0.30, "h": 0.68}
      ],
      "llm_summary": "一名男子正经过客厅走向门口。",
      "llm_latency_ms": 5840.2,
      "image_url": "/api/v1/images/2026-11-23/evt_1795412000_1.jpg",
      "created_at": "2026-11-23 05:33:20"
    }
  ]
}
```

`total` 是数据库全部事件数，不受分页影响。`targets` 从 `targets_json` 解析；响应不包含 Base64 图片、内部 `image_rel_path` 或宿主机绝对路径。`image_url` 是相对 **cloud-storage** 地址的资源 URL，不能拼到边缘或 LLM 地址上。`created_at` 是 SQLite 自动生成的 UTC 入库时间，与事件发生时间 `timestamp_ms` 分开。

### 5.4 读取历史图片

`GET :8090/api/v1/images/{date}/{filename}` 返回 `200 Content-Type: image/jpeg` 和未经 OSD 绘制的干净抓拍。`date` 必须为实际 UTC 日历日期 `YYYY-MM-DD`，`filename` 只能是合法 `<event_id>.jpg`。服务端只在 `cloud-storage/storage/images/` 根目录下解析；拒绝路径分隔符、`..`、编码绕过和越出根目录的最终路径。非法路径返回 400，图片不存在返回 404，均采用统一 Error。

## 6. 上游 llama-server（外部依赖）与异步投递

`GenAiWorker` 单后台线程、待分析队列容量 1，只保留最新待处理事件。使用同一份干净 JPEG Base64 顺序执行：

1. 若 `llm.enabled` 为 true，向 `llm.endpoint` 请求摘要；关闭、失败或超时则摘要为空字符串，继续下一步。连接超时默认 3 秒，读取超时默认 25 秒。
2. 若 `storage.enabled` 为 true，向 `storage.endpoint` 投递事件和上述 JPEG；存储 HTTP 超时默认 5 秒。失败仅记录简短日志并丢弃本次投递，不在魔百盒落盘重试或缓存历史数据，不阻塞实时流水线。

原生外部接口为 `GET http://192.168.10.10:8080/health` 和 `POST http://192.168.10.10:8080/v1/chat/completions`，不修改上游服务端：

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

成功取 `choices[0].message.content`，随后与事件元数据合并投递 PC 云存储，不回填边缘历史表。JPEG 使用 `stbi_write_jpg_to_func` 在内存中编码，默认质量 70；应在 OSD 展示绘制前取得干净画面。同一事件由活动状态聚合与冷却机制触发，规则见架构和路线图。

本节 LLM 两个接口不纳入本仓库 OpenAPI paths。后续需与实际 llama-server 版本联调核对多模态支持，不能据此宣称已部署或已验收。配置实际凭据时由客户端发送相应认证信息，凭据不得在配置响应、日志或 Issue 中回显。
