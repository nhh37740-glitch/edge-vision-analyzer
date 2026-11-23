# 云存储模拟服务

联想 PC 上的独立进程，使用 FastAPI + SQLite 实现，默认监听 `0.0.0.0:8090`。它接收魔百盒异步投递的干净 JPEG、结构化事件与 Fluent Bit 日志，提供历史分页查询和图片读取。服务与魔百盒的 C++ 构建、ARM64 交叉编译完全解耦。接口语义见 [API 契约](../docs/api.md)，机器契约见 [OpenAPI](../docs/openapi.yaml)。

## 启动与运行

需要 Python 3.10 或更高版本。为避免把虚拟环境放进仓库，可在家目录创建：

```sh
cd cloud-storage
python3 -m venv ~/.venvs/cloud-storage
source ~/.venvs/cloud-storage/bin/activate
python -m pip install -r requirements.txt
python -m uvicorn server:app --host 0.0.0.0 --port 8090
```

Windows 上激活命令为 `~/.venvs/cloud-storage/Scripts/activate`。`server:app` 需要在 `cloud-storage/` 目录内启动。服务启动时自动创建数据目录、依据 [schema.sql](schema.sql) 初始化 `events.db`，无需手工建表。

`:8090` 与 `llama-server :8080` 是各自独立的进程，可独立启停。启动后可探活：

```sh
curl http://127.0.0.1:8090/health
# {"status":"ok","db":"connected"}
```

默认数据根目录为 `cloud-storage/storage/`，可用环境变量 `CLOUD_STORAGE_DIR` 指向其他目录。所有图片、SQLite 数据库和日志都写入该目录，整目录已被 Git 忽略。

## 已实现端点

| 方法 | 路径 | 行为 |
| --- | --- | --- |
| GET | `/health` | SQLite 连接健康探针；未就绪返回 503 统一 Error。 |
| POST | `/api/v1/events` | 校验字段与 JPEG，按有效 `timestamp_ms` 的 UTC 日期写入 `images/YYYY-MM-DD/<event_id>.jpg` 和 SQLite；成功返回 201。重复 `event_id` 返回 400 `EVENT_EXISTS`，不覆盖原记录。 |
| GET | `/api/v1/events?limit=20&offset=0` | 按 `timestamp_ms DESC`、同毫秒 `event_id DESC` 分页；`limit` 范围 1..100，`offset` 非负。 |
| GET | `/api/v1/images/{date}/{filename}` | 返回 `image/jpeg` 干净抓拍；拒绝路径分隔符、`..` 与越出根目录的路径。 |
| POST | `/api/v1/logs` | 接收 JSON 对象数组，按 PC 接收时的 UTC 日期逐条追加到 `logs/YYYY-MM-DD.log`；成功返回 204。 |
| GET | `/api/v1/logs?date=YYYY-MM-DD&tail=100` | 以 `application/x-ndjson` 返回指定日期最近 `tail` 行（保持写入顺序）；`tail` 范围 1..1000。 |

统一错误体为 `{"error": {"code": "...", "message": "..."}}`。服务端时间兜底、字段范围和错误语义均以 API 与 OpenAPI 契约为准。

## 数据布局

- `storage/images/YYYY-MM-DD/<event_id>.jpg`：Base64 解码后的干净 JPEG。
- `storage/events.db`：`events` 表与 `idx_events_timestamp` 索引，结构以 [schema.sql](schema.sql) 为唯一依据；`image_rel_path` 以 `storage/` 为根，如 `images/2026-11-23/evt_1795412000_1.jpg`。
- `storage/logs/YYYY-MM-DD.log`：每行一条紧凑 JSON 日志对象，不写入 SQLite。

## 离线测试

测试使用 FastAPI `TestClient` 与临时目录，不访问网络、不依赖真实图片或运行数据库：

```sh
python -m pip install pytest httpx
python -m pytest -q cloud-storage
```

也可在 `cloud-storage/` 目录内执行 `python -m pytest -q`。`requirements.txt` 只锁定服务运行依赖，测试所需的 `pytest` 与 `httpx` 需另行安装。
