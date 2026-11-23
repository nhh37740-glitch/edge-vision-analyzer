#!/usr/bin/env bash
# 魔百盒部署：对时 → 版本 → 发包 → 重启 → 探活 → 日志。只发送构建产物与公开配置模板。
set -u
HOST=${1:-${EDGE_BOX_HOST:-}}
BIN=${2:-}
CONF=${3:-}
SERVICE=${4:-edge-vision}
if [ -z "$HOST" ] || [ -z "$BIN" ] || [ -z "$CONF" ]; then
  echo "用法: $0 <host> <binary> <config-template> [service]；host 缺省读 EDGE_BOX_HOST" >&2
  exit 2
fi
# 配置模板可落到目标机的 config.local.json，但源文件不能是本机私有配置。
case "$CONF" in *config.local.json) echo "拒绝发送私有配置: $CONF" >&2; exit 2;; esac
log_tail() { ssh "$HOST" "journalctl -u '$SERVICE' -n 20 --no-pager"; }
fail() { echo "失败: $1" >&2; log_tail >&2; exit 1; }
echo "== 1/6 对时（如提示密码，请输入目标机 sudo 密码）=="
ssh -t "$HOST" "sudo date -s '@$(date +%s)'" || fail "对时"
echo "== 2/6 系统与库版本 =="
ssh "$HOST" '. /etc/os-release 2>/dev/null; echo "OS: ${PRETTY_NAME:-unknown}"; uname -a; echo "CPU/SoC: $(lscpu 2>/dev/null | sed -n "s/^Model name:[[:space:]]*//p" | head -1) $(tr -d "\0" </proc/device-tree/model 2>/dev/null)"; free -h; ldd --version 2>/dev/null | head -1; ffmpeg -version 2>/dev/null | head -1' || fail "系统与库版本"
echo "== 3/6 发送构建产物与配置模板 =="
scp "$BIN" "$HOST:/opt/edge-vision-analyzer/bin/" || fail "scp 构建产物"
scp "$CONF" "$HOST:/opt/edge-vision-analyzer/config/config.local.json" || fail "scp 配置模板"
echo "== 4/6 重启 $SERVICE =="
ssh -t "$HOST" "sudo systemctl restart '$SERVICE'" || fail "重启服务"
echo "== 5/6 探活 http://127.0.0.1:8081/api/v1/health =="
ssh "$HOST" 'i=0; while [ "$i" -lt 20 ]; do curl -sf --max-time 2 http://127.0.0.1:8081/api/v1/health >/dev/null && exit 0; i=$((i+1)); sleep 1; done; exit 1' || fail "健康检查"
echo "== 6/6 最近 20 行服务日志 =="
log_tail || exit 1
