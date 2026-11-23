CREATE TABLE IF NOT EXISTS events (
    event_id TEXT PRIMARY KEY,
    frame_id INTEGER NOT NULL,
    timestamp_ms INTEGER NOT NULL,
    top_label TEXT NOT NULL,
    top_score REAL NOT NULL,
    targets_json TEXT NOT NULL,
    llm_summary TEXT,
    llm_latency_ms REAL DEFAULT 0.0,
    image_rel_path TEXT NOT NULL,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_events_timestamp ON events(timestamp_ms DESC);
