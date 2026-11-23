# 云存储模拟服务

联想 PC 上的独立进程，在 `v0.3-nous` 阶段实现。它接收魔百盒异步投递的事件、检测结果、可选场景描述和未加 OSD 的 JPEG，并提供持久化查询。运行入口为 `server.py`，当前为空占位。Python 依赖见 [requirements.txt](requirements.txt)。

## 启动（待实现）

完成 `v0.3-nous` 的 FastAPI 应用后，在联想 PC 的 Python 虚拟环境内，从仓库的 `cloud-storage/` 目录运行：

```sh
python -m pip install -r requirements.txt
python -m uvicorn server:app --host 0.0.0.0 --port 8090
```

`:8090` 与 `llama-server :8080` 是各自独立的进程，可独立启停。通过联想 PC 实际局域网地址访问。服务完成后，`GET /health` 返回 SQLite 可连接状态。接口、分页和图像访问约定见 [API 契约](../docs/api.md)。该服务独立于魔百盒的 C++ 构建和部署。

## 文件与 SQLite 契约

- `storage/images/YYYY-MM-DD/<event_id>.jpg` 存储 Base64 解码后未经 OSD 的 JPEG。`image_rel_path` 以 `storage/` 为根，格式为 `images/YYYY-MM-DD/<event_id>.jpg`。日期从事件 `timestamp_ms` 按 UTC 取日。
- `storage/events.db` 的 `events` 表以 `event_id` 为主键，保存 `frame_id`、`timestamp_ms`、最高分标签和置信度、`targets_json`、可选 `llm_summary` 与 `llm_latency_ms`、`image_rel_path` 及自动生成的 `created_at`。`idx_events_timestamp` 支持按事件时间倒序查询。精确定义见 [schema.sql](schema.sql)。
- 预计由 `server.py` 初始化目录和表。成功写入图片和对应数据库记录后才返回 201。历史查询返回 `image_url`，调用方用其读取 JPEG。
- `storage/` 由服务在运行时创建，整目录已被 Git 忽略。抓拍、数据库及其附属文件保留在联想 PC。
