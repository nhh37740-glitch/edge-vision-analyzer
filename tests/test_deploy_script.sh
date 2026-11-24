#!/usr/bin/env bash
# 用桩 ssh/scp 验证 deploy_to_box.sh 的配置保留与原子写入行为。
# CTest 只在 UNIX 注册；Windows 可手动运行本脚本。
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
printf '%s\n' "$*" >> "$SSH_LOG"
case "$*" in
  *"test -f /opt/edge-vision-analyzer/config/config.local.json"*)
    state=${REMOTE_CONFIG_STATE:-missing}
    [ "$state" = "banner" ] && echo "banner line"
    case "$state" in
      present|banner) exit 0 ;;
      missing) exit 1 ;;
      *) exit 255 ;;
    esac
    ;;
esac
exit 0
EOF
chmod +x "$TMP/bin/scp" "$TMP/bin/ssh"

CONFIG_TARGET="test-host:/opt/edge-vision-analyzer/config/config.local.json.new"
BIN_TARGET="test-host:/opt/edge-vision-analyzer/bin/edge-vision-analyzer.new"
CHECK_CMD='test -f /opt/edge-vision-analyzer/config/config.local.json'

fail() {
  echo "FAIL: $1" >&2
  cat "$TMP/out.log" >&2 || true
  exit 1
}

reset_logs() {
  : > "$TMP/scp.log"
  : > "$TMP/ssh.log"
}

run_case() {
  local state=$1 force=${2:-0}
  reset_logs
  PATH="$TMP/bin:$PATH" SCP_LOG="$TMP/scp.log" SSH_LOG="$TMP/ssh.log" \
  REMOTE_CONFIG_STATE="$state" EDGE_CONFIG_FORCE="$force" \
    bash "$SCRIPT" test-host "$BIN" "$CONF" edge-vision > "$TMP/out.log" 2>&1 ||
    fail "部署脚本返回非零（$state/force=$force）"
  grep -Fq "$CHECK_CMD" "$TMP/ssh.log" || fail "未检查目标机配置路径"
}

assert_scp_count() {
  [ "$(wc -l < "$TMP/scp.log")" -eq "$1" ] || fail "scp 次数应为 $1"
}

assert_has_config_upload() {
  grep -Fq "$CONFIG_TARGET" "$TMP/scp.log" || fail "未上传到预期配置路径"
}

assert_no_config_upload() {
  if grep -Fq 'config.local.json' "$TMP/scp.log"; then fail "不应上传配置模板"; fi
}

# 已有配置且未强制：只上传二进制，保留现场 config.local.json。
run_case present 0
assert_scp_count 1
grep -Fq "$BIN_TARGET" "$TMP/scp.log" || fail "未上传到预期二进制路径"
assert_no_config_upload
grep -Fq '保留现场配置' "$TMP/out.log" || fail "缺少保留配置提示"

# 远端有登录横幅时仍按 sentinel 判断，不能把 present 误判为异常。
run_case banner 0
assert_scp_count 1
assert_no_config_upload
grep -Fq '保留现场配置' "$TMP/out.log" || fail "横幅场景未保留配置"

# 目标机缺少配置：原子上传并安装模板。
run_case missing 0
assert_scp_count 2
assert_has_config_upload

# 显式强制：已有配置也覆盖。
run_case present 1
assert_scp_count 2
assert_has_config_upload
grep -Fq '已按 EDGE_CONFIG_FORCE=1' "$TMP/out.log" || fail "缺少强制覆盖提示"

# 配置检查失败（SSH 255）：必须中止且不能按 missing 上传配置。
reset_logs
if PATH="$TMP/bin:$PATH" SCP_LOG="$TMP/scp.log" SSH_LOG="$TMP/ssh.log" \
  REMOTE_CONFIG_STATE=error EDGE_CONFIG_FORCE=0 \
  bash "$SCRIPT" test-host "$BIN" "$CONF" edge-vision > "$TMP/out.log" 2>&1; then
  fail "配置检查失败时部署应返回非零"
fi
assert_scp_count 1
assert_no_config_upload
grep -Fq '失败: 检查目标机配置' "$TMP/out.log" || fail "缺少检查失败提示"

echo "deploy script config preservation: OK"
