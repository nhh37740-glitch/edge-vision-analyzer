"""联想 PC 云存储服务。

独立的 FastAPI + SQLite 进程，默认监听 :8090。负责接收魔百盒异步投递的
干净 JPEG 与结构化事件、提供历史分页查询和图片读取，并接收／查询
Fluent Bit 结构化日志。运行数据全部位于 storage/ 目录，不参与边缘 C++ 构建。
"""

from __future__ import annotations

import base64
import binascii
import json
import os
import re
import sqlite3
import sys
import time
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

from fastapi import FastAPI, Query
from fastapi.exceptions import RequestValidationError
from fastapi.responses import FileResponse, JSONResponse, Response
from pydantic import BaseModel, ConfigDict, Field, field_validator
from starlette.exceptions import HTTPException as StarletteHTTPException

SERVICE_VERSION = "0.1.0"

# PATCH-009 有效事件时间范围：2025-01-01T00:00:00Z 至 9999-12-31T23:59:59.999Z。
MIN_EFFECTIVE_TIMESTAMP_MS = 1735689600000
MAX_EFFECTIVE_TIMESTAMP_MS = 253402300799999
INT64_MAX = 9223372036854775807

_DATE_PATTERN = re.compile(r"^\d{4}-\d{2}-\d{2}$")
_FILENAME_PATTERN = re.compile(r"^[A-Za-z0-9_-]+\.jpg$")
_EVENT_ID_PATTERN = r"^[A-Za-z0-9_-]+$"
_JPEG_MAGIC = b"\xff\xd8\xff"
_EPOCH = datetime(1970, 1, 1, tzinfo=timezone.utc)

_BASE_DIR = Path(__file__).resolve().parent
_SCHEMA_PATH = _BASE_DIR / "schema.sql"


def _error(status_code: int, code: str, message: str) -> JSONResponse:
    """统一错误体；不包含磁盘路径或原始输入内容。"""
    return JSONResponse(
        status_code=status_code,
        content={"error": {"code": code, "message": message}},
    )


def _validation_message(exc: RequestValidationError) -> str:
    errors = exc.errors()
    if not errors:
        return "invalid request"
    first = errors[0]
    location = ".".join(
        str(part)
        for part in first.get("loc", ())
        if part not in ("body", "query", "path")
    )
    message = str(first.get("msg", "invalid value"))
    return f"{location}: {message}" if location else message


def _utc_date_from_ms(timestamp_ms: int) -> str:
    moment = _EPOCH + timedelta(milliseconds=timestamp_ms)
    return moment.strftime("%Y-%m-%d")


def _is_valid_utc_date(value: str) -> bool:
    if not _DATE_PATTERN.fullmatch(value):
        return False
    try:
        datetime.strptime(value, "%Y-%m-%d")
    except ValueError:
        return False
    return True


def _now_ms() -> int:
    return int(time.time() * 1000)


def _init_storage(storage_dir: Path) -> None:
    (storage_dir / "images").mkdir(parents=True, exist_ok=True)
    (storage_dir / "logs").mkdir(parents=True, exist_ok=True)
    schema = _SCHEMA_PATH.read_text(encoding="utf-8")
    connection = sqlite3.connect(storage_dir / "events.db")
    try:
        connection.executescript(schema)
        connection.commit()
    finally:
        connection.close()


def _database_ready(db_path: Path) -> bool:
    try:
        connection = sqlite3.connect(db_path)
        try:
            connection.execute("SELECT 1 FROM events LIMIT 1").fetchone()
        finally:
            connection.close()
    except sqlite3.Error:
        return False
    return True


class TargetBox(BaseModel):
    model_config = ConfigDict(extra="forbid")

    class_id: int | None = Field(default=None, ge=0)
    label: str
    score: float = Field(ge=0.0, le=1.0, allow_inf_nan=False)
    x: float = Field(ge=0.0, le=1.0, allow_inf_nan=False)
    y: float = Field(ge=0.0, le=1.0, allow_inf_nan=False)
    w: float = Field(ge=0.0, le=1.0, allow_inf_nan=False)
    h: float = Field(ge=0.0, le=1.0, allow_inf_nan=False)


class CreateEventRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    event_id: str = Field(min_length=1, pattern=_EVENT_ID_PATTERN)
    frame_id: int = Field(ge=0, le=INT64_MAX)
    timestamp_ms: int = Field(ge=0, le=INT64_MAX)
    top_label: str
    top_score: float = Field(ge=0.0, le=1.0, allow_inf_nan=False)
    targets: list[TargetBox]
    llm_summary: str
    llm_latency_ms: float = Field(ge=0.0, allow_inf_nan=False)
    image_base64: str = Field(min_length=1)

    @field_validator("frame_id", "timestamp_ms", mode="before")
    @classmethod
    def _require_integer(cls, value: Any) -> Any:
        # JSON 布尔值在 Python 中是 int 的子类，需单独拒绝。
        if isinstance(value, bool) or not isinstance(value, int):
            raise ValueError("must be an integer")
        return value


def create_app(storage_dir: str | Path | None = None) -> FastAPI:
    """创建云存储应用；测试可传入临时目录，部署默认使用 storage/。"""
    if storage_dir is None:
        base_dir = Path(os.environ.get("CLOUD_STORAGE_DIR", _BASE_DIR / "storage"))
    else:
        base_dir = Path(storage_dir)
    base_dir = base_dir.resolve()
    db_path = base_dir / "events.db"
    images_root = base_dir / "images"
    logs_root = base_dir / "logs"

    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        try:
            _init_storage(base_dir)
        except (OSError, sqlite3.Error) as exc:
            # 启动失败不阻止服务进程，健康检查会如实报告数据库未就绪。
            print(f"cloud-storage: storage init failed: {exc}", file=sys.stderr)
        yield

    app = FastAPI(
        title="Edge Vision Analyzer Cloud Storage",
        version=SERVICE_VERSION,
        lifespan=lifespan,
    )

    @app.exception_handler(RequestValidationError)
    async def _on_request_validation(_request, exc: RequestValidationError):
        return _error(400, "INVALID_PARAMETER", _validation_message(exc))

    @app.exception_handler(StarletteHTTPException)
    async def _on_http_error(_request, exc: StarletteHTTPException):
        code = "NOT_FOUND" if exc.status_code == 404 else "HTTP_ERROR"
        return _error(exc.status_code, code, str(exc.detail))

    @app.exception_handler(Exception)
    async def _on_unexpected_error(_request, _exc: Exception):
        return _error(500, "INTERNAL_ERROR", "internal server error")

    @app.get("/health")
    def health():
        if not _database_ready(db_path):
            return _error(503, "DB_UNAVAILABLE", "database is not ready")
        return {"status": "ok", "db": "connected"}

    @app.post("/api/v1/events", status_code=201)
    def create_event(payload: CreateEventRequest):
        received_ms = _now_ms()
        timestamp_ms = payload.timestamp_ms
        if (
            timestamp_ms < MIN_EFFECTIVE_TIMESTAMP_MS
            or timestamp_ms > MAX_EFFECTIVE_TIMESTAMP_MS
        ):
            timestamp_ms = received_ms

        date_str = _utc_date_from_ms(timestamp_ms)
        image_rel_path = f"images/{date_str}/{payload.event_id}.jpg"
        image_path = base_dir / image_rel_path
        targets = [target.model_dump(exclude_none=True) for target in payload.targets]

        try:
            jpeg = base64.b64decode(payload.image_base64, validate=True)
        except (binascii.Error, ValueError):
            return _error(400, "INVALID_IMAGE", "image_base64 is not valid Base64")
        if not jpeg.startswith(_JPEG_MAGIC):
            return _error(400, "INVALID_IMAGE", "image_base64 is not a JPEG image")

        # 先查重，避免覆盖已有留存。
        try:
            connection = sqlite3.connect(db_path)
            try:
                exists = connection.execute(
                    "SELECT 1 FROM events WHERE event_id = ?", (payload.event_id,)
                ).fetchone()
            finally:
                connection.close()
        except sqlite3.Error:
            return _error(500, "INTERNAL_ERROR", "database operation failed")
        if exists:
            return _error(400, "EVENT_EXISTS", "event_id already exists")

        try:
            image_path.parent.mkdir(parents=True, exist_ok=True)
            # "xb" 以独占方式创建，避免并发请求覆盖同一事件图片。
            with open(image_path, "xb") as image_file:
                image_file.write(jpeg)
        except FileExistsError:
            return _error(400, "EVENT_EXISTS", "event_id already exists")
        except OSError:
            return _error(500, "INTERNAL_ERROR", "failed to store image")

        targets_json = json.dumps(
            targets, ensure_ascii=False, separators=(",", ":")
        )
        try:
            connection = sqlite3.connect(db_path)
            try:
                connection.execute(
                    "INSERT INTO events ("
                    "event_id, frame_id, timestamp_ms, top_label, top_score, "
                    "targets_json, llm_summary, llm_latency_ms, image_rel_path"
                    ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    (
                        payload.event_id,
                        payload.frame_id,
                        timestamp_ms,
                        payload.top_label,
                        payload.top_score,
                        targets_json,
                        payload.llm_summary,
                        payload.llm_latency_ms,
                        image_rel_path,
                    ),
                )
                connection.commit()
            finally:
                connection.close()
        except sqlite3.IntegrityError:
            image_path.unlink(missing_ok=True)
            return _error(400, "EVENT_EXISTS", "event_id already exists")
        except sqlite3.Error:
            image_path.unlink(missing_ok=True)
            return _error(500, "INTERNAL_ERROR", "database operation failed")

        return {
            "status": "created",
            "event_id": payload.event_id,
            "image_url": f"/api/v1/images/{date_str}/{payload.event_id}.jpg",
        }

    @app.get("/api/v1/events")
    def list_events(
        limit: int = Query(20, ge=1, le=100),
        offset: int = Query(0, ge=0),
    ):
        try:
            connection = sqlite3.connect(db_path)
            connection.row_factory = sqlite3.Row
            try:
                total = connection.execute("SELECT COUNT(*) FROM events").fetchone()[0]
                rows = connection.execute(
                    "SELECT event_id, frame_id, timestamp_ms, top_label, top_score, "
                    "targets_json, llm_summary, llm_latency_ms, image_rel_path, "
                    "created_at FROM events "
                    "ORDER BY timestamp_ms DESC, event_id DESC LIMIT ? OFFSET ?",
                    (limit, offset),
                ).fetchall()
            finally:
                connection.close()
        except sqlite3.Error:
            return _error(500, "INTERNAL_ERROR", "database operation failed")

        events = []
        for row in rows:
            try:
                targets = json.loads(row["targets_json"])
            except (TypeError, ValueError):
                targets = []
            events.append(
                {
                    "event_id": row["event_id"],
                    "frame_id": row["frame_id"],
                    "timestamp_ms": row["timestamp_ms"],
                    "top_label": row["top_label"],
                    "top_score": row["top_score"],
                    "targets": targets,
                    "llm_summary": row["llm_summary"] or "",
                    "llm_latency_ms": row["llm_latency_ms"],
                    "image_url": f"/api/v1/{row['image_rel_path']}",
                    "created_at": row["created_at"],
                }
            )
        return {"total": total, "limit": limit, "offset": offset, "events": events}

    @app.get("/api/v1/images/{date}/{filename:path}")
    def get_image(date: str, filename: str):
        if not _is_valid_utc_date(date):
            return _error(
                400, "INVALID_PARAMETER", "date must be a valid YYYY-MM-DD UTC date"
            )
        if not _FILENAME_PATTERN.fullmatch(filename):
            return _error(
                400, "INVALID_PARAMETER", "filename must be a valid <event_id>.jpg"
            )
        image_root = images_root.resolve()
        candidate = (images_root / date / filename).resolve()
        if not candidate.is_relative_to(image_root):
            return _error(400, "INVALID_PARAMETER", "invalid image path")
        if not candidate.is_file():
            return _error(404, "NOT_FOUND", "image not found")
        return FileResponse(candidate, media_type="image/jpeg")

    @app.post("/api/v1/logs", status_code=204)
    def append_logs(records: list[dict[str, Any]]):
        received = datetime.now(timezone.utc)
        log_path = logs_root / f"{received.strftime('%Y-%m-%d')}.log"
        if not records:
            return Response(status_code=204)
        payload = "".join(
            json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n"
            for record in records
        )
        try:
            log_path.parent.mkdir(parents=True, exist_ok=True)
            with open(log_path, "a", encoding="utf-8", newline="\n") as log_file:
                log_file.write(payload)
        except OSError:
            return _error(500, "INTERNAL_ERROR", "failed to write logs")
        return Response(status_code=204)

    @app.get("/api/v1/logs")
    def tail_logs(
        date: str = Query(...),
        tail: int = Query(100, ge=1, le=1000),
    ):
        if not _is_valid_utc_date(date):
            return _error(
                400, "INVALID_PARAMETER", "date must be a valid YYYY-MM-DD UTC date"
            )
        log_path = logs_root / f"{date}.log"
        try:
            data = log_path.read_bytes()
        except FileNotFoundError:
            return _error(404, "NOT_FOUND", "log file not found")
        except OSError:
            return _error(500, "INTERNAL_ERROR", "failed to read logs")
        lines = data.splitlines(keepends=True)
        if len(lines) > tail:
            lines = lines[-tail:]
        return Response(content=b"".join(lines), media_type="application/x-ndjson")

    return app


app = create_app()


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=8090)
