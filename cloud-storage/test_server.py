"""cloud-storage 离线测试。

使用 FastAPI TestClient 和 tmp_path，不访问网络、不使用真实图片或运行数据库。
"""

import base64
import json
import sqlite3
import time
from datetime import datetime, timezone

import pytest
from fastapi.testclient import TestClient

import server

JPEG = (
    b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    b"\xff\xd9"
)
JPEG_ALT = (
    b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x01"
    b"\xff\xd9"
)


def _base64(data: bytes = JPEG) -> str:
    return base64.b64encode(data).decode("ascii")


def _event(
    event_id: str,
    timestamp_ms: int,
    jpeg: bytes = JPEG,
    **overrides,
) -> dict:
    payload = {
        "event_id": event_id,
        "frame_id": 88210,
        "timestamp_ms": timestamp_ms,
        "top_label": "person",
        "top_score": 0.87,
        "targets": [
            {"label": "person", "score": 0.87, "x": 0.21, "y": 0.15, "w": 0.3, "h": 0.68}
        ],
        "llm_summary": "一名男子正经过客厅走向门口。",
        "llm_latency_ms": 5840.2,
        "image_base64": _base64(jpeg),
    }
    payload.update(overrides)
    return payload


@pytest.fixture
def client(tmp_path):
    app = server.create_app(tmp_path)
    with TestClient(app) as test_client:
        yield test_client


def test_health_reports_connected_database(client, tmp_path):
    response = client.get("/health")

    assert response.status_code == 200
    assert response.json() == {"status": "ok", "db": "connected"}
    assert (tmp_path / "events.db").is_file()


def test_event_create_and_pagination_boundary(client, tmp_path):
    records = [
        ("evt_a", 1795412000000),
        ("evt_b", 1795412001000),
        ("evt_c", 1795412001000),
    ]
    for event_id, timestamp_ms in records:
        response = client.post("/api/v1/events", json=_event(event_id, timestamp_ms))
        assert response.status_code == 201, response.text
        body = response.json()
        assert body["status"] == "created"
        assert body["event_id"] == event_id
        assert body["image_url"].startswith("/api/v1/images/")

    page = client.get("/api/v1/events", params={"limit": 2, "offset": 0})
    assert page.status_code == 200
    body = page.json()
    assert body["total"] == 3
    assert body["limit"] == 2
    assert body["offset"] == 0
    # timestamp_ms DESC，同毫秒按 event_id DESC 稳定排序。
    assert [event["event_id"] for event in body["events"]] == ["evt_c", "evt_b"]

    first = body["events"][0]
    assert set(first) == {
        "event_id",
        "frame_id",
        "timestamp_ms",
        "top_label",
        "top_score",
        "targets",
        "llm_summary",
        "llm_latency_ms",
        "image_url",
        "created_at",
    }
    assert first["targets"] == [
        {"label": "person", "score": 0.87, "x": 0.21, "y": 0.15, "w": 0.3, "h": 0.68}
    ]
    assert first["image_url"] == "/api/v1/images/2026-11-23/evt_c.jpg"

    second_page = client.get("/api/v1/events", params={"limit": 2, "offset": 2})
    assert [event["event_id"] for event in second_page.json()["events"]] == ["evt_a"]
    empty_page = client.get("/api/v1/events", params={"limit": 1, "offset": 3})
    assert empty_page.status_code == 200
    assert empty_page.json()["events"] == []

    assert client.get("/api/v1/events", params={"limit": 100}).status_code == 200
    assert client.get("/api/v1/events", params={"limit": 101}).status_code == 400
    assert client.get("/api/v1/events", params={"limit": 0}).status_code == 400
    assert client.get("/api/v1/events", params={"offset": -1}).status_code == 400
    assert client.get("/api/v1/events", params={"limit": "abc"}).status_code == 400

    image = client.get(first["image_url"])
    assert image.status_code == 200
    assert image.headers["content-type"].startswith("image/jpeg")
    assert image.content == JPEG

    missing = client.get("/api/v1/images/2026-11-23/evt_missing.jpg")
    assert missing.status_code == 404
    assert missing.json()["error"]["code"] == "NOT_FOUND"
    assert client.get("/api/v1/images/2026-02-30/evt_a.jpg").status_code == 400
    assert client.get("/api/v1/images/2026-11-23/evt_a.txt").status_code == 400

    invalid_base64 = client.post(
        "/api/v1/events",
        json=_event("evt_bad_base64", 1795412000000, image_base64="not base64!"),
    )
    assert invalid_base64.status_code == 400
    not_jpeg = client.post(
        "/api/v1/events",
        json=_event("evt_bad_jpeg", 1795412000000, jpeg=b"not-a-jpeg"),
    )
    assert not_jpeg.status_code == 400

    date = (
        datetime.fromtimestamp(1795412001000 / 1000, tz=timezone.utc)
        .strftime("%Y-%m-%d")
    )
    connection = sqlite3.connect(tmp_path / "events.db")
    try:
        row = connection.execute(
            "SELECT timestamp_ms, image_rel_path FROM events WHERE event_id = ?",
            ("evt_c",),
        ).fetchone()
    finally:
        connection.close()
    assert row == (1795412001000, f"images/{date}/evt_c.jpg")
    assert (tmp_path / row[1]).is_file()


def test_duplicate_event_returns_conflict_without_overwrite(client, tmp_path):
    created = client.post("/api/v1/events", json=_event("evt_dup", 1795412000123))
    assert created.status_code == 201
    image_url = created.json()["image_url"]
    assert client.get(image_url).content == JPEG

    duplicate = client.post(
        "/api/v1/events", json=_event("evt_dup", 1795412000999, jpeg=JPEG_ALT)
    )
    assert duplicate.status_code == 400
    assert duplicate.json()["error"]["code"] == "EVENT_EXISTS"

    listing = client.get("/api/v1/events")
    assert listing.status_code == 200
    assert listing.json()["total"] == 1
    # 原抓拍未被第二次请求覆盖。
    assert client.get(image_url).content == JPEG
    assert (tmp_path / "images").exists()


def test_log_append_and_tail(client, tmp_path):
    first_batch = [
        {
            "date": "2026-11-23T05:33:20.123Z",
            "MESSAGE": "采集节点已启动",
            "_SYSTEMD_UNIT": "edge-vision.service",
        },
        {"level": "info", "nested": {"a": 1}, "MESSAGE": "第一行\n第二行"},
    ]
    appended = client.post("/api/v1/logs", json=first_batch)
    assert appended.status_code == 204
    assert appended.content == b""

    second_batch = [{"MESSAGE": "second batch", "value": 3.5}]
    assert client.post("/api/v1/logs", json=second_batch).status_code == 204

    log_files = list((tmp_path / "logs").glob("*.log"))
    assert len(log_files) == 1
    date = log_files[0].stem

    tailed = client.get("/api/v1/logs", params={"date": date, "tail": 2})
    assert tailed.status_code == 200
    assert tailed.headers["content-type"].startswith("application/x-ndjson")
    tail_lines = tailed.content.decode("utf-8").splitlines()
    assert len(tail_lines) == 2
    assert json.loads(tail_lines[0]) == first_batch[1]
    assert json.loads(tail_lines[1]) == second_batch[0]
    # 对象内换行被转义为单个 NDJSON 行，解析后仍保留。
    assert json.loads(tail_lines[0])["MESSAGE"] == "第一行\n第二行"

    all_lines = client.get("/api/v1/logs", params={"date": date, "tail": 1000})
    assert [json.loads(line) for line in all_lines.content.decode("utf-8").splitlines()] == (
        first_batch + second_batch
    )

    assert client.get("/api/v1/logs", params={"date": date, "tail": 0}).status_code == 400
    assert client.get("/api/v1/logs", params={"date": date, "tail": 1001}).status_code == 400
    assert client.get("/api/v1/logs", params={"date": "2026-02-30"}).status_code == 400
    assert client.get("/api/v1/logs", params={"date": "2000-01-01"}).status_code == 404

    assert client.post("/api/v1/logs", json={"MESSAGE": "not an array"}).status_code == 400
    assert client.post("/api/v1/logs", json=[1, 2]).status_code == 400


def test_timestamp_fallback_uses_receive_time(client, tmp_path):
    before = int(time.time() * 1000)
    response = client.post("/api/v1/events", json=_event("evt_fallback", 0))
    after = int(time.time() * 1000)
    assert response.status_code == 201

    event = client.get("/api/v1/events").json()["events"][0]
    assert before <= event["timestamp_ms"] <= after
    date = (
        datetime.fromtimestamp(event["timestamp_ms"] / 1000, tz=timezone.utc)
        .strftime("%Y-%m-%d")
    )
    assert event["image_url"] == f"/api/v1/images/{date}/evt_fallback.jpg"
    assert (tmp_path / "images" / date / "evt_fallback.jpg").is_file()
    assert client.get(event["image_url"]).status_code == 200
