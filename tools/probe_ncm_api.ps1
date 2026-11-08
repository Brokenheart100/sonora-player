# 网易云常用接口探通（N0-1）。
#
# 只做只读 GET，不发任何写操作。产物是一份日志：每个端点一行"状态 + code + 片段"。
# 用法：
#   powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/probe_ncm_api.ps1 -Out docs/probe/ncm-anon.log
#   powershell.exe ... -Cookie "MUSIC_U=...; __csrf=..." -Out docs/probe/ncm-cookie.log
#
# 为什么要有这个脚本：这些是未公开的 web 接口，没有兼容承诺。先探通、把真实
# 返回记下来，再决定写不写代码——R0 就吃过一次亏（/api/login/qrcode/create 是 404）。
param(
    [string]$Cookie = "",
    [string]$Out = ""
)

$ErrorActionPreference = "Continue"
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch { }

$base = "https://music.163.com"
$ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"
$headers = @{
    "User-Agent" = $ua
    "Referer"    = "https://music.163.com/"
    "Accept"     = "application/json, text/plain, */*"
}
if ($Cookie -ne "") { $headers["Cookie"] = $Cookie }

$script:Lines = New-Object System.Collections.Generic.List[string]
$script:RawDir = ""
function Emit([string]$text) {
    $script:Lines.Add($text)
    Write-Host $text
}

function Invoke-Probe([string]$id, [string]$name, [string]$url) {
    Emit "=== $id $name"
    Emit "url: $url"
    try {
        $resp = Invoke-WebRequest -Uri $url -Headers $headers -UseBasicParsing -TimeoutSec 20
        $status = [int]$resp.StatusCode
        $body = [string]$resp.Content
    }
    catch {
        $status = -1
        $body = ""
        try { $status = [int]$_.Exception.Response.StatusCode.value__ } catch { }
        try {
            $reader = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
            $body = $reader.ReadToEnd()
        } catch { }
        if ($body -eq "" ) { $body = "[exception] " + $_.Exception.Message }
    }
    $code = "?"
    $m = [regex]::Match($body, '"code"\s*:\s*(-?\d+)')
    if ($m.Success) { $code = $m.Groups[1].Value }
    $snippet = $body
    if ($snippet.Length -gt 260) { $snippet = $snippet.Substring(0, 260) }
    $snippet = $snippet -replace "[\r\n]+", " "
    Emit "status: $status"
    Emit "code: $code"
    Emit "body: $snippet"
    if ($script:RawDir -ne "") {
        Set-Content -Path (Join-Path $script:RawDir ($id + ".json")) -Value $body -Encoding UTF8
    }
    return $body
}

function Try-Json([string]$text) {
    try { return ($text | ConvertFrom-Json) } catch { return $null }
}

Emit "probe started: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Emit "cookie: $(if ($Cookie -eq '') { 'anonymous' } else { 'present (' + $Cookie.Length + ' chars)' })"

if ($Out -ne "") {
    $raw = Join-Path (Split-Path -Parent $Out) "raw"
    if (-not (Test-Path $raw)) { New-Item -ItemType Directory -Path $raw | Out-Null }
    $script:RawDir = $raw
}

# 1) 先用搜索拿到 song / artist / album 三个真实 id，后面串起来用。
$body = Invoke-Probe "1" "search 晴天" "$base/api/search/get?s=$([uri]::EscapeDataString('晴天'))&type=1&offset=0&limit=5"
$j = Try-Json $body
$songId = ""
$artistId = ""
$albumId = ""
if ($j -ne $null -and $j.result -ne $null -and $j.result.songs -ne $null -and $j.result.songs.Count -gt 0) {
    $first = $j.result.songs[0]
    $songId = [string]$first.id
    if ($first.artists -ne $null -and $first.artists.Count -gt 0) { $artistId = [string]$first.artists[0].id }
    if ($first.album -ne $null) { $albumId = [string]$first.album.id }
}
Emit "derived: songId=$songId artistId=$artistId albumId=$albumId"

# 2) 榜单列表，取第一张榜的 playlist id。
$body = Invoke-Probe "2" "toplist" "$base/api/toplist"
$j = Try-Json $body
$playlistId = ""
if ($j -ne $null -and $j.list -ne $null -and $j.list.Count -gt 0) { $playlistId = [string]$j.list[0].id }
Emit "derived: playlistId=$playlistId"

if ($playlistId -ne "") {
    Invoke-Probe "3" "playlist detail" "$base/api/v6/playlist/detail?id=$playlistId" | Out-Null
    Invoke-Probe "4" "playlist track all" "$base/api/playlist/track/all?id=$playlistId&limit=10&offset=0" | Out-Null
} else {
    Emit "=== 3/4 skipped: no playlistId"
}
if ($albumId -ne "") {
    Invoke-Probe "5" "album" "$base/api/v1/album/$albumId" | Out-Null
} else { Emit "=== 5 skipped: no albumId" }
if ($artistId -ne "") {
    Invoke-Probe "6" "artist" "$base/api/v1/artist/$artistId" | Out-Null
    Invoke-Probe "7" "artist songs" "$base/api/v1/artist/songs?id=$artistId&limit=5&order=hot" | Out-Null
} else { Emit "=== 6/7 skipped: no artistId" }
Invoke-Probe "8" "personalized playlists" "$base/api/personalized/playlist?limit=6" | Out-Null
Invoke-Probe "9" "new songs" "$base/api/personalized/newsong?limit=5" | Out-Null
if ($songId -ne "") {
    Invoke-Probe "10" "similar songs" "$base/api/discovery/simiSong?id=$songId" | Out-Null
    Invoke-Probe "11" "lyric" "$base/api/song/lyric?id=$songId&lv=-1&tv=-1" | Out-Null
} else { Emit "=== 10/11 skipped: no songId" }

# 12) 需要登录的端点：匿名跑一遍，看它们是 301 / 302 / 空列表。写操作不探。
Invoke-Probe "12" "daily recommend songs" "$base/api/v3/discovery/recommend/songs" | Out-Null
Invoke-Probe "13" "daily recommend playlists" "$base/api/v1/discovery/recommend/resource" | Out-Null
Invoke-Probe "14" "user playlists" "$base/api/user/playlist?uid=0&limit=5&offset=0" | Out-Null
Invoke-Probe "15" "personal fm" "$base/api/v1/radio/get" | Out-Null

Emit "probe finished: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

if ($Out -ne "") {
    $dir = Split-Path -Parent $Out
    if ($dir -ne "" -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
    Set-Content -Path $Out -Value $script:Lines -Encoding UTF8
    Write-Output "log written: $Out"
}
