#!/usr/bin/env bash
# 列出上游 metasequoiaime/msime-windows 自上次审阅以来、改到本仓仍保留文件的提交，供人工挑选对照修复。
# 用法：bash scripts/upstream-review.sh            列出待看提交
#       bash scripts/upstream-review.sh --mark      把上游当前 develop 记为已审阅
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
git remote get-url upstream >/dev/null 2>&1 || git remote add upstream https://github.com/metasequoiaime/msime-windows
git fetch -q upstream develop
marker=.upstream-reviewed
base=$(cat "$marker" 2>/dev/null || echo cd9a34f)   # 分叉点：上游 v0.13.2
if [ "${1:-}" = --mark ]; then git rev-parse upstream/develop > "$marker"; echo "已记录 $(cat $marker)"; exit 0; fi
for c in $(git rev-list --reverse --no-merges "$base"..upstream/develop); do
  kept=$(git show --name-only --format= "$c" | while read -r f; do [ -e "$f" ] && echo "$f"; done; true)
  [ -z "$kept" ] && continue
  echo "$(git log -1 --format='%h %ad %s' --date=short "$c")"
  echo "$kept" | sed 's/^/    /' | head -5
done
