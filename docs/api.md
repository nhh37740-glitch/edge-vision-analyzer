# Edge Vision Analyzer — 双服务 API 契约

状态：有效设计契约；`v0.1-oculus` 的 health、stats、config 读取与 source-switch，`v0.2-discern` 的 PATCH config 原子热更新，`v1.0-vox` 的 snapshot 与 stream，以及 `v0.3-nous` 的云存储 health/events/images/logs 均已实现。边缘端点已在本机回环与离线用例验证，云存储由离线 pytest 覆盖，真机验收待执行。维护责任：项目维护 Agent。更新日期：2026-11-24（北京时间）。依据：PATCH-001 至 PATCH-009；存储与历史查询以 PATCH-003 为准，配置与局域网服务范围以 PATCH-007 为准，日志接口以 PATCH-008 为准，SoC 温度观测与事件时间兜底以 PATCH-009 为准。

本轮不宣称真机验收；已实现范围仅指本机回环与离线用例验证。机器可读契约见 [openapi.yaml](openapi.yaml)，系统边界见 [architecture.md](architecture.md)，交付与离线验收见 [roadmap.md](roadmap.md)，开发和执行边界遵守 [AGENTS.md](../AGENTS.md)。公开配置模板见 [config/config.example.json](../config/config.example.json)，运行时使用被 Git 忽略的 `config/config.local.json`。PC 数据表契约见 [schema.sql](../cloud-storage/schema.sql)。

## 1. 两个服务的地址与职责

| 服务 | 示例地址 | 唯一职责 |
| --- | --- | --- |
| 魔百盒边缘端 | `http://192.168.10.11:8081` | 实时控制、状态与带 OSD 的实时画面 |
| 联想 PC cloud-storage | `http://192.168.10.10:8090` | 留存干净 JPEG、SQLite 结构化事件、历史查询与日志收集 |
| 联想 PC llama-server（外部依赖） | `http://192.168.10.10:8080` | 多模态语义推理，不承担留存与历史查询 |

全部服务只在同一有线局域网内通信，推理固定使用联想 PC `:8080` 的本地 `llama-server`，留存使用同机 `:8090` 的 `cloud-storage`，不接公有云服务。边缘进程计划监听 `0.0.0.0:8081`，云存储进程计划监听 `0.0.0.0:8090`。调用时使用实际设备地址。OpenAPI 的每个路径分别设置 `servers`，避免把不同服务当作可互换的服务器列表。

边缘端共 6 个资源路径、7 个操作：

| 方法 | 路径 | 计划里程碑 | 契约 |
| --- | --- | --- | --- |
| GET | `/api/v1/health` | `v0.1-oculus` | 存活、版本、时长、节点线程及 LLM/Storage 连通状态 |
| GET | `/api/v1/stats` | `v0.1-oculus` | FPS、跳过率、耗时、丢帧、RSS 及可选 SoC 温度 |
| GET | `/api/v1/config` | `v0.1-oculus` | 读取当前生效配置 |
| PATCH | `/api/v1/config` | `v0.2-discern`，存储开关于 `v0.3-nous` 接入 | 原子热更新检测/LLM/Storage 五项字段 |
| POST | `/api/v1/source/switch` | `v0.1-oculus` | 切换手机流/本地文件；失败保留原源 |
| GET | `/api/v1/snapshot.jpg` | `v1.0-vox` | 实时 OSD JPEG；无首帧返回 503 |
| GET | `/api/v1/stream` | `v1.0-vox` | 实时 OSD MJPEG |

云存储端共 4 个资源路径、6 个操作，均属于 `v0.3-nous`。两服务合计 10 个资源路径、13 个操作：

| 方法 | 路径 | 契约 |
| --- | --- | --- |
| GET | `/health` | 数据库连接健康探针 |
| POST | `/api/v1/events` | 接收干净抓拍与结构化结果，成功返回 201 |
| GET | `/api/v1/events` | 以 `limit/offset` 分页查询历史事件 |
| GET | `/api/v1/images/{date}/{filename}` | 返回对应历史事件的干净 JPEG |
| POST | `/api/v1/logs` | 接收 Fluent Bit JSON 对象数组，逐条追加日志文件 |
| GET | `/api/v1/logs` | 按接收日期查询最近日志行 |

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

400 表示字段、参数、源初始化或图像输入非法；404 表示资源不存在；500 表示内部错误，包括文件/SQLite 写入失败；503 表示边缘画面/节点或云数据库未就绪。错误信息不得暴露磁盘路径。JSON 字段与范围维护在 OpenAPI 中。

## 2. 边缘健康与统计

`GET :8081/api/v1/health` 成功示例：

```json
{
  "status": "ok",
  "version": "v0.1-oculus",
  "uptime_sec": 125.4,
  "nodes": [
    {"name": "CaptureNode", "state": "running"}
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

节点状态为 `starting/running/stopping/stopped/failed/not_enabled`。两个上游分别报告 `disabled/reachable/unreachable/unknown`。只有 `CaptureNode` 失败时边缘返回 200、`status: degraded`；LLM/Storage 的上游状态由后台探活线程维护：启用时立即探测一次并按约 2 秒间隔刷新，单次 `GET <base>/health` 的超时上限为 500ms，200 报告 `reachable`，连接失败、超时或非 200 报告 `unreachable`，首轮完成前短暂报告 `unknown`，关闭时报告 `disabled`。探活结果只缓存并更新对应字段，不改变边缘的 200 与 `status`；health 请求只读取缓存，不等待上游；探活只发送 GET，不触发真实多模态生成或存储写入。上述行为已在本机回环与离线用例验证，未做真机验收。

`GET :8081/api/v1/stats` 成功示例：

```json
{
  "capture_fps": 14.8,
  "detect_fps": 4.2,
  "motion_skip_ratio": 0.72,
  "avg_infer_ms": 84.5,
  "avg_llm_latency_ms": 6210.0,
  "dropped_frames": 12,
  "rss_memory_mb": 142.6,
  "soc_temp_c": 62.5
}
```

保留原统计中无冲突的平均 LLM 耗时和累计丢帧指标。未启用或尚无样本的阶段指标返回 0。`dropped_frames` 已由 Issue #55 实现：统计节点输入队列满时丢弃的最旧帧和节点停止时丢弃的队列帧，并与 Metrics 累计值合并返回；无丢帧时为 0。无状态边缘节点 RSS 目标严格 **小于 150MB**；实际超限仍须如实报告测量值。

可选字段 `soc_temp_c` 为可空数字，单位摄氏度。读取 Linux `/sys/class/thermal/thermal_zone0/temp` 的毫摄氏度值并除以 1000，例如 62500 对应 `62.5`；读取或解析失败返回 `null`，不使用 0 代替错误。该值仅供压测观测，不触发限频、停机或其他温控动作。Linux 实现已具备，目标机实测待补。

## 3. 边缘配置读取与热更新

`GET :8081/api/v1/config` 返回当前生效配置，覆盖 [config.example.json](../config/config.example.json) 的全部字段：

| 配置块 | 字段 |
| --- | --- |
| `input` | `source`, `loop`, `width`, `height`, `target_fps`, `dump_interval_sec` |
| `detector` | `motion_mad_threshold`, `model_param`, `model_bin`, `input_size`, `conf_threshold`, `num_threads`, `target_labels`, `quantization`, `model_candidates` |
| `event` | `end_grace_sec` |
| `llm` | `enabled`, `endpoint`, `model`, `quantization`, `model_candidates`, `prompt`, `cooldown_sec`, `connect_timeout_sec`, `read_timeout_sec`, `jpeg_quality` |
| `storage` | `enabled`, `endpoint`, `timeout_sec` |
| `streamer` | `mode`, `http_port`, `rtsp_url` |

`event.max_events_in_memory` 已移除。OpenAPI `Config/LlmConfig` 与公开配置模板字段一致；本地地址等运行参数写入 `config/config.local.json`，该文件不提交仓库。

`detector.quantization` 取 `auto`（默认）、`fp32`、`fp16` 或 `int8`。`detector.model_candidates` 为可选对象，键只能是 `fp32`、`fp16`、`int8`，每项包含成对的 `param` 与 `bin`。`auto` 在设备支持 int8（aarch64 dotprod/asimddp）、可用内存不小于 256 MB 且存在 int8 候选时选 int8，否则在支持 fp16 且内存不小于 128 MB 时选 fp16，否则使用 `model_param`/`model_bin` 基础模型。

```json
{
  "detector": {
    "model_param": "models/yolo-fastest-v2.param",
    "model_bin": "models/yolo-fastest-v2.bin",
    "quantization": "auto",
    "model_candidates": {
      "fp16": {"param": "models/yolo-fastest-v2-fp16.param", "bin": "models/yolo-fastest-v2-fp16.bin"},
      "int8": {"param": "models/yolo-fastest-v2-int8.param", "bin": "models/yolo-fastest-v2-int8.bin"}
    }
  }
}
```

`PATCH :8081/api/v1/config` 已实现，使用嵌套结构：

```json
{
  "detector": {"motion_mad_threshold": 8.0, "conf_threshold": 0.5},
  "llm": {"enabled": true, "cooldown_sec": 15},
  "storage": {"enabled": true}
}
```

仅允许五个叶子字段：`detector.motion_mad_threshold`（0..255）、`detector.conf_threshold`（0..1）、`llm.enabled`（布尔）、`llm.cooldown_sec`（非负整数秒数）、`storage.enabled`（布尔）。原四项在 `v0.2-discern` 支持，存储开关在 `v0.3-nous` 接入。可只提交一个字段；拒绝空对象、未知字段、错误类型及范围错误，统一返回 400/`INVALID_PARAMETER`，错误消息不包含服务器路径。

先整份校验请求，再在配置锁内一次性发布完整的配置快照；任一字段失败则全部不变，GET 立即读到旧值或完整新值而不会读到部分更新。成功返回 200 和完整生效配置（与 GET 相同的 `to_json`），不持久化到磁盘。已注入的运行节点在快照发布后通过原子 setter 生效：MAD 阈值用于下一帧，置信度用于下一帧检测与过滤，冷却时间用于下一次触发，LLM/存储开关用于后续任务。`GenAiWorker` 在进程启动后始终运行，两个开关都关闭时不编码、不发 HTTP；开关只决定后续任务是否编码和投递，不改变 worker 的启动/停止生命周期。视频源走专用切源接口；模型、线程数、端口以及 `storage.endpoint/storage.timeout_sec` 仅静态配置，不能热更。

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

切回手机可使用 `{"source":"http://192.168.10.12:8080/video","loop":false}`，也可使用实际 RTSP 地址。两个字段必填，`source` 为非空字符串；字段缺失或类型错误返回 400/`INVALID_PARAMETER`。新源打开并完成解码初始化后才提交切换；打开失败、解码初始化失败或录制期间切源返回 400/`INVALID_SOURCE`，继续原视频源。成功后 GET 配置反映新的 `input.source/input.loop`。本地路径相对进程工作目录，交付时以仓库/部署资源根目录运行。

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

为处理魔百盒尚未对时的事件，服务端按以下契约确定一次 **有效 `timestamp_ms`**：

- 入站字段必填，必须是非负 `int64` 整数；`0` 是合法输入。缺失、负值、字符串、非整数、布尔值或超出 `int64` 范围返回 400，不作为未对时兜底。
- 对合法输入，若 `timestamp_ms < 1735689600000`（即早于 **2025-01-01T00:00:00Z**），使用联想 PC 接收本次请求时取得的当前 Unix 毫秒；该接收时间只取一次。晚于 `9999-12-31T23:59:59.999Z`、无法生成四位年份图片目录的合法整数也使用同一兜底。
- 若时间处于 `2025-01-01T00:00:00Z` 至 `9999-12-31T23:59:59.999Z` 范围内（含两端），保留调用方时间。
- 选定的同一个有效时间同时用于 SQLite `timestamp_ms`、历史列表返回与排序，以及图片文件夹的 UTC 日期；不能在各步骤分别取当前时间。`event_id` 和其他事件字段保持原值。

下面仅为规则示例，假设 PC 接收时间为 `1795412000123`，不代表已运行服务：

| 入站 `timestamp_ms` | 有效 `timestamp_ms` | 图片 UTC 日期 |
| --- | --- | --- |
| `0` | `1795412000123` | `2026-11-23` |
| `1735689599999` | `1795412000123` | `2026-11-23` |
| `1735689600000` | `1735689600000` | `2025-01-01` |

上方正常请求示例的时间 `1795412000123` 保持不变，对应 `2026-11-23`。图片写入 `cloud-storage/storage/images/2026-11-23/evt_1795412000_1.jpg`，SQLite 写入 `cloud-storage/storage/events.db`。`image_rel_path` 以 `storage/` 为根，存为 `images/2026-11-23/evt_1795412000_1.jpg`；`targets` 序列化为 `targets_json`。完整列定义与索引见 [schema.sql](../cloud-storage/schema.sql)，不在此重复维护建表 SQL。

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

`GET :8090/api/v1/events?limit=20&offset=0`：`limit` 默认 20、范围 1..100；`offset` 默认 0、为非负整数。非法值返回 400。按入库时已经确定的有效 `timestamp_ms DESC` 排序，同毫秒以 `event_id DESC` 稳定排序。

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

`total` 是数据库全部事件数，不受分页影响。`targets` 从 `targets_json` 解析；响应不包含 Base64 图片、内部 `image_rel_path` 或宿主机绝对路径。`image_url` 是相对 **cloud-storage** 地址的资源 URL，不能拼到边缘或 LLM 地址上。历史列表的 `timestamp_ms` 是入库时已确定的有效时间，图片 URL 的日期来自同一个值。`created_at` 是 SQLite 自动生成的 UTC 入库时间，与这个有效事件时间分开。

### 5.4 读取历史图片

`GET :8090/api/v1/images/{date}/{filename}` 返回 `200 Content-Type: image/jpeg` 和未经 OSD 绘制的干净抓拍。`date` 来自入库时选定的有效 `timestamp_ms`，必须为实际 UTC 日历日期 `YYYY-MM-DD`；`filename` 只能是合法 `<event_id>.jpg`。服务端只在 `cloud-storage/storage/images/` 根目录下解析；拒绝路径分隔符、`..`、编码绕过和越出根目录的最终路径。非法路径返回 400，图片不存在返回 404，均采用统一 Error。

### 5.5 接收结构化日志

`POST :8090/api/v1/logs` 供魔百盒 Fluent Bit HTTP output 使用，发送 `Content-Type: application/json`，`Format json` 每次提交 **JSON 对象数组**：

```json
[
  {
    "date": "2026-11-23T05:33:20.123Z",
    "MESSAGE": "采集节点已启动",
    "_SYSTEMD_UNIT": "edge-vision.service"
  }
]
```

这是结构示例，`date`、`MESSAGE`、`_SYSTEMD_UNIT` 等均非接口强制字段；保留每条对象的原始结构和全部 JSON 字段。时间可以是 ISO8601 字符串或实际输出的其他 JSON 值，接收端不重命名、删减或补造字段。仓库 [Fluent Bit 模板](../deploy/fluent-bit.conf) 当前使用 `Format json`，未固定时间字段格式；最终字段以机器上实际模板及 Fluent Bit 输出为准。时间键与格式可由输出插件配置，见 [Fluent Bit HTTP 官方文档](https://docs.fluentbit.io/manual/data-pipeline/outputs/http)。

**实现约定**：在联想 PC 接收请求时确定 UTC 日期，整个批次归档至 `cloud-storage/storage/logs/YYYY-MM-DD.log`；不依据日志对象内的源时间字段划分文件。每条对象序列化为一行紧凑 UTF-8 JSON，保留字段结构，字符串内换行转义，逐条追加并以换行结束。日志不写 SQLite，也不改变事件表。

全部追加完成后返回 **204 No Content**，无响应体。非法 JSON、非数组或数组含非对象元素返回 400 和统一 Error；文件写入失败返回 500，不能回报 204。接口按追加语义处理，重试批次可能产生重复行。

### 5.6 查询日志尾部

`GET :8090/api/v1/logs?date=2026-11-23&tail=100` 仅查询 PC 已接收的本地日志，不请求或代理魔百盒接口。

- `date` 必填，必须是 `YYYY-MM-DD` 的真实 UTC 日历日期；格式正确但日期不存在也返回 400。
- `tail` 默认 100，范围 1..1000，必须为整数；非法值返回 400。
- 从对应日期文件读取最近 N 行，并保持这些行原有的写入顺序（较早的在前）；不足 N 行返回全部，空文件返回 200 和空内容。

成功返回 **200 Content-Type: application/x-ndjson**，不是 JSON 数组；每行对应一条原始日志对象。例如：

```jsonl
{"date":"2026-11-23T05:33:20.123Z","MESSAGE":"采集节点已启动","_SYSTEMD_UNIT":"edge-vision.service"}
```

文件不存在返回 404 和统一 Error；日期/条数非法返回 400，读取失败返回 500。服务端仅从 `cloud-storage/storage/logs/` 下按已校验日期拼接文件名，不接受调用方路径或返回物理路径。本接口与 POST 均只在交换机局域网内使用，由 PC `:8090` 独立提供，魔百盒 `:8081` 不新增日志 API。

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

请求中的 `model` 由 `llm.quantization` 与 `llm.model_candidates` 选型得到：`auto` 依次尝试 q8、q5、q4，显式档位不可用或全部不可用时回退到基础 `llm.model`，此时 `quantization` 记为 `none`。启动时尝试 `GET /v1/models` 获取上游已加载的模型别名，失败按未知列表处理；显存已知时要求不低于档位门槛（q8 8192MB、q5 6144MB、q4 4096MB，0 表示未知不限制），延迟已知时还要求不超过 `read_timeout_sec`。

本节 LLM 两个接口不纳入本仓库 OpenAPI paths。后续需与实际 llama-server 版本联调核对多模态支持，不能据此宣称已部署或已验收。
