#!/usr/bin/env bash
# 用桩 ssh/scp 验证 deploy_to_box.sh 的配置保留行为，不访问网络与真实设备。
set -eu
SCRIPT=${1:?usage: test_deploy_script.sh <deploy-script>}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/work"

BIN="$TMP/work/edge-vision-analyzer"
CONF="$TMP/work/config.example.json"
printf '#!/bin/sh\nexit 0\n' > "$BIN"
printf '{}\n' > "$CONF"

cat > "$TMP/bin/scp" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$SCP_LOG"
EOF

cat > "$TMP/bin/ssh" <<'EOF'
#!/bin/sh
case "$*" in
  *config.local.json*) echo "${REMOTE_CONFIG_STATE:-missing}" ;;
esac
exit 0
EOF
chmod +x "$TMP/bin/scp" "$TMP/bin/ssh"

fail() {
  echo "FAIL: $1" >&2
  cat "$TMP/out.log" >&2 || true
  exit 1
}

run_case() {
  : > "$TMP/scp.log"
  PATH="$TMP/bin:$PATH" \
  SCP_LOG="$TMP/scp.log" \
  REMOTE_CONFIG_STATE="$1" \
  EDGE_CONFIG_FORCE="${2:-0}" \
    bash "$SCRIPT" test-host "$BIN" "$CONF" edge-vision > "$TMP/out.log" 2>&1 || fail "部署脚本返回非零（$1/force=$2）"
}

# 目标机已有配置且未强制：只上传二进制，保留现场 config.local.json。
run_case present 0
[ "$(wc -l < "$TMP/scp.log")" -eq 1 ] || fail "已有配置时不应上传配置模板"
grep -q 'edge-vision-analyzer.new' "$TMP/scp.log" || fail "未上传二进制"
grep -q '保留现场配置' "$TMP/out.log" || fail "缺少保留配置提示"

# 目标机缺少配置：上传二进制和配置模板。
run_case missing 0
[ "$(wc -l < "$TMP/scp.log")" -eq 2 ] || fail "缺少配置时应上传模板"
grep -q 'config.local.json' "$TMP/scp.log" || fail "未上传配置模板"

# 显式强制：已有配置也覆盖。
run_case present 1
[ "$(wc -l < "$TMP/scp.log")" -eq 2 ] || fail "EDGE_CONFIG_FORCE=1 时应覆盖配置"
grep -q 'config.local.json' "$TMP/scp.log" || fail "强制模式未上传配置模板"

echo "deploy script config preservation: OK"
