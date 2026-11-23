#!/usr/bin/env bash
# 联想 PC 双服务：llama-server 监听 :8080，cloud-storage 监听 :8090。
# 用法: LLM_MODEL=/模型.gguf LLM_MMPROJ=/mmproj.gguf bash scripts/start_pc_services.sh
# 可选环境变量：LLM_PORT(8080)、STORAGE_PORT(8090)、LLM_ALIAS(vlm-local)、
# LOG_DIR(临时目录)、PROBE_TRIES(60)。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LLM_PORT=${LLM_PORT:-8080}; STORAGE_PORT=${STORAGE_PORT:-8090}; LLM_ALIAS=${LLM_ALIAS:-vlm-local}
if [ -z "${LLM_MODEL:-}" ] || [ -z "${LLM_MMPROJ:-}" ]; then
  echo "缺少模型路径。用法: LLM_MODEL=<模型.gguf> LLM_MMPROJ=<mmproj.gguf> $0" >&2
  echo "可选: LLM_PORT=8080 STORAGE_PORT=8090 LLM_ALIAS=vlm-local LOG_DIR=<日志目录> PROBE_TRIES=60" >&2
  exit 2
fi
missing() { echo "缺少 $1。$2" >&2; exit 2; }
command -v llama-server >/dev/null 2>&1 || missing llama-server "请从 llama.cpp 构建并加入 PATH（参考 docs/build.md）。"
command -v uvicorn >/dev/null 2>&1 || missing uvicorn "请安装: python3 -m pip install -r \"$ROOT/cloud-storage/requirements.txt\""
command -v curl >/dev/null 2>&1 || missing curl "探活需要 curl，请先安装。"
LOG_DIR=${LOG_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/pc-services.XXXXXX")} || exit 1
mkdir -p "$LOG_DIR"; LLM_LOG="$LOG_DIR/llama-server.log"; STORAGE_LOG="$LOG_DIR/cloud-storage.log"; PIDS=""
cleanup_hint() { echo "已启动进程: $PIDS。停止: kill $PIDS" >&2; }
fail() { echo "失败: $1" >&2; tail -n 20 "$2" >&2; cleanup_hint; exit 1; }
echo "启动 llama-server :$LLM_PORT，日志: $LLM_LOG"
llama-server --model "$LLM_MODEL" --mmproj "$LLM_MMPROJ" --alias "$LLM_ALIAS" --host 0.0.0.0 --port "$LLM_PORT" >"$LLM_LOG" 2>&1 &
LLM_PID=$!; PIDS="$LLM_PID"; sleep 1
kill -0 "$LLM_PID" 2>/dev/null || fail "llama-server 启动" "$LLM_LOG"
echo "启动 cloud-storage :$STORAGE_PORT，日志: $STORAGE_LOG"
( cd "$ROOT/cloud-storage" && exec uvicorn server:app --host 0.0.0.0 --port "$STORAGE_PORT" ) >"$STORAGE_LOG" 2>&1 &
STORAGE_PID=$!; PIDS="$LLM_PID $STORAGE_PID"; sleep 1
kill -0 "$STORAGE_PID" 2>/dev/null || fail "cloud-storage 启动" "$STORAGE_LOG"
probe() { i=0; while [ "$i" -lt "${PROBE_TRIES:-60}" ]; do curl -sf --max-time 2 "http://127.0.0.1:$1/health" >/dev/null && return 0; i=$((i+1)); sleep 1; done; return 1; }
probe "$LLM_PORT" || fail "llama-server :$LLM_PORT 探活超时" "$LLM_LOG"
probe "$STORAGE_PORT" || fail "cloud-storage :$STORAGE_PORT 探活超时" "$STORAGE_LOG"
echo "就绪: llama-server :$LLM_PORT，cloud-storage :$STORAGE_PORT；PID $PIDS；日志 $LOG_DIR"
