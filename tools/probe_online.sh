#!/usr/bin/env bash
# Sonora R0 探针 —— 网易云接口可用性验证
#
# 只手工运行，不进 CI、不建 C++ target（见 docs/ONLINE.md §11.2 R0-1）。
# 真实响应原样落盘到 tests/fixtures/online/<label>.json，作为后续单测夹具。
#
# 用法：
#   bash tools/probe_online.sh all                       # 依次跑全部子命令
#   bash tools/probe_online.sh search --keyword 晴天
#   bash tools/probe_online.sh detail --ids 186016,347230
#   bash tools/probe_online.sh url    --ids 186016 --level exhigh
#   bash tools/probe_online.sh lyric  --ids 186016
#   bash tools/probe_online.sh account [--cookie "MUSIC_U=...; __csrf=..."]
#   bash tools/probe_online.sh ratelimit --count 20 --interval 200
#   bash tools/probe_online.sh tls                       # 看 TLS 后端与证书链
#
# 环境变量：
#   PROBE_BASE    默认 https://music.163.com（可指向镜像/本地桩）
#   PROBE_COOKIE  等价于 --cookie，避免 cookie 出现在 shell 历史里
#   PROBE_OUT     默认 tests/fixtures/online
set -uo pipefail

BASE="${PROBE_BASE:-https://music.163.com}"
OUT="${PROBE_OUT:-tests/fixtures/online}"
COOKIE="${PROBE_COOKIE:-}"
UA="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36"

KEYWORD="晴天"
IDS="186016"
LEVEL="standard"
COUNT=20
INTERVAL=200

mkdir -p "$OUT"

die() { printf 'error: %s\n' "$*" >&2; exit 2; }

# ── 一次请求：落盘 body + headers，打印一行摘要 ───────────────────────────────
call() { # $1=label  $2=url
    local label="$1" url="$2"
    local body="$OUT/$label.json" head="$OUT/$label.headers"
    local meta
    local hdr=(-H "User-Agent: $UA" -H "Referer: $BASE/" -H "Origin: $BASE" -H "Accept: */*")
    [ -n "$COOKIE" ] && hdr+=(-H "Cookie: $COOKIE")
    meta=$(curl -sS --compressed --max-time 20 \
        -o "$body" -D "$head" \
        -w '%{http_code} %{time_total} %{size_download}' \
        "${hdr[@]}" \
        "$url") || { printf '[FAIL] %-12s curl 失败\n' "$label"; return 1; }

    local http time size
    read -r http time size <<<"$meta"
    printf '[ok] %-12s HTTP %s  %ss  %sB\n' "$label" "$http" "$time" "$size"
    summarize "$label" "$body"
}

# ── 摘要：只读句点（python 是仓库既有依赖，jq 没有装）───────────────────────
summarize() { # $1=label $2=bodyfile
    python -X utf8 - "$1" "$2" <<'PY'
import json, sys, pathlib

label, path = sys.argv[1], pathlib.Path(sys.argv[2])
raw = path.read_text(encoding="utf-8", errors="replace")
if not raw.strip():
    print("   body: <空>")
    sys.exit()
try:
    d = json.loads(raw)
except Exception:
    print(f"   body: <非 JSON，前 80 字>{raw[:80]!r}")
    sys.exit()

code = d.get("code")
msg = d.get("msg") or d.get("message") or ""
print(f"   code={code} {('msg=' + str(msg)) if msg else ''}".rstrip())

def show_songs(container):
    songs = container or []
    print(f"   songs={len(songs)}")
    for s in songs[:3]:
        artists = "/".join(a.get("name", "?") for a in (s.get("artists") or s.get("ar") or []))
        album = (s.get("album") or s.get("al") or {}).get("name", "?")
        print(f"     id={s.get('id')} name={s.get('name')!r} artists={artists!r} album={album!r} dt={s.get('duration') or s.get('dt')}")

if label.startswith("search"):
    r = d.get("result") or {}
    print(f"   songCount={r.get('songCount')} hasMore={r.get('hasMore')}")
    show_songs(r.get("songs"))
elif label.startswith("detail"):
    songs = d.get("songs") or []
    print(f"   songs={len(songs)} requested={len(d.get('privileges') or [])}")
    for s in songs[:2]:
        artists = "/".join(a.get("name", "?") for a in (s.get("ar") or s.get("artists") or []))
        album = (s.get("al") or s.get("album") or {}).get("name", "?")
        print(f"     id={s.get('id')} name={s.get('name')!r} artists={artists!r} album={album!r} "
              f"dt={s.get('dt') or s.get('duration')} fee={s.get('fee')} st={s.get('st')}")
elif label.startswith("url"):
    data = d.get("data") or []
    resolved = [x for x in data if x.get("url")]
    codes = {}
    for x in data:
        codes[x.get("code")] = codes.get(x.get("code"), 0) + 1
    print(f"   total={len(data)} resolved={len(resolved)} code_hist={codes}")
    for item in data[:2]:
        u = item.get("url")
        print(f"   id={item.get('id')} code={item.get('code')} fee={item.get('fee')} level={item.get('level')!r} "
              f"br={item.get('br')} size={item.get('size')} expi={item.get('expi')} freeTrial={bool(item.get('freeTrialInfo'))}")
        print(f"     url={'<null>' if not u else u[:64] + ('…' if len(u) > 64 else '')}")
        if u:
            import urllib.parse as up
            q = up.urlparse(u)
            print(f"     host={q.netloc} scheme={q.scheme} query_keys={sorted(k for k in up.parse_qs(q.query))}")
elif label.startswith("lyric"):
    for key in ("lrc", "tlyric", "klyric", "romalrc"):
        block = (d.get(key) or {}).get("lyric") or ""
        first = next((ln for ln in block.splitlines() if ln.strip()), "")
        print(f"   {key}: {len(block)} 字符  首行={first[:60]!r}")
elif label.startswith("account"):
    acct = d.get("account") or {}
    prof = d.get("profile") or {}
    print(f"   account={'<空>' if not acct else acct.get('id')} nickname={prof.get('nickname')!r} "
          f"vipType={prof.get('vipType')} code={d.get('code')}")
PY
}

# ── 子命令 ───────────────────────────────────────────────────────────────────
cmd_search() { # /api/search/get 与 /api/cloudsearch/pc 各一次
    local q
    q=$(urlencode "$KEYWORD")
    call search_legacy "$BASE/api/search/get?s=$q&type=1&limit=10&offset=0&total=true"
    call search_cloud  "$BASE/api/cloudsearch/pc?s=$q&type=1&limit=10&offset=0&total=true"
}

urlencode() { python -X utf8 -c "import sys,urllib.parse as u;print(u.quote(sys.argv[1]))" "$1"; }

cmd_detail() {
    local c
    c=$(python -X utf8 -c "import json,sys;print(json.dumps([{'id':int(i)} for i in sys.argv[1].split(',')],separators=(',',':')))" "$IDS")
    call detail "$BASE/api/v3/song/detail?c=$(urlencode "$c")"
}

cmd_url() {
    local ids
    ids=$(python -X utf8 -c "import json,sys;print(json.dumps([int(i) for i in sys.argv[1].split(',')],separators=(',',':')))" "$IDS")
    call url "$BASE/api/song/enhance/player/url/v1?ids=$(urlencode "$ids")&level=$LEVEL&encodeType=aac"
}

cmd_lyric() {
    call lyric "$BASE/api/song/lyric?id=$IDS&lv=1&kv=1&tv=-1"
}

cmd_account() {
    call account "$BASE/api/nuser/account/get"
}

cmd_ratelimit() {
    local i http time code rl="$OUT/.ratelimit.tmp.json"
    local hdr=(-H "User-Agent: $UA" -H "Referer: $BASE/")
    [ -n "$COOKIE" ] && hdr+=(-H "Cookie: $COOKIE")
    printf 'ratelimit: %s 次，间隔 %sms\n' "$COUNT" "$INTERVAL"
    for i in $(seq 1 "$COUNT"); do
        read -r http time <<<"$(curl -sS --compressed --max-time 15 -o "$rl" -w '%{http_code} %{time_total}' \
            "${hdr[@]}" \
            "$BASE/api/search/get?s=$(urlencode "$KEYWORD")&type=1&limit=10&offset=0")"
        code=$(python -X utf8 -c "import json,sys;print(json.load(open(sys.argv[1],encoding='utf-8')).get('code'))" "$rl" 2>/dev/null || echo '?')
        printf '  #%02d HTTP %s code=%s %ss\n' "$i" "$http" "$code" "$time"
        [ "$i" -lt "$COUNT" ] && sleep "$(python -c "print($INTERVAL/1000)")"
    done
    rm -f "$rl"
}

cmd_tls() {
    echo "=== curl 后端 ==="
    curl --version | head -2
    echo "=== 握手细节 ==="
    curl -sS -o /dev/null -v --max-time 15 "$BASE/" 2>&1 \
        | grep -iE "SSL connection|TLS|subject:|issuer:|ALPN|certificate" | head -20
}

usage() {
    sed -n '2,26p' "$0"
}

# ── 参数解析 ─────────────────────────────────────────────────────────────────
[ $# -ge 1 ] || { usage; exit 2; }
CMD="$1"; shift
while [ $# -gt 0 ]; do
    case "$1" in
        --cookie)   COOKIE="$2"; shift 2 ;;
        --keyword)  KEYWORD="$2"; shift 2 ;;
        --ids)      IDS="$2"; shift 2 ;;
        --level)    LEVEL="$2"; shift 2 ;;
        --count)    COUNT="$2"; shift 2 ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --out)      OUT="$2"; shift 2 ;;
        -h|--help)  usage; exit 0 ;;
        *) die "未知参数：$1" ;;
    esac
done

mkdir -p "$OUT"

case "$CMD" in
    search)    cmd_search ;;
    detail)    cmd_detail ;;
    url)       cmd_url ;;
    lyric)     cmd_lyric ;;
    account)   cmd_account ;;
    ratelimit) cmd_ratelimit ;;
    tls)       cmd_tls ;;
    all)
        cmd_tls
        echo
        cmd_search
        cmd_detail
        cmd_url
        cmd_lyric
        cmd_account
        ;;
    *) die "未知子命令：$CMD（search/detail/url/lyric/account/ratelimit/tls/all）" ;;
esac
