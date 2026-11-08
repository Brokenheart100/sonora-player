# Sonora 打包脚本（M9）：产出一个能在没装 Qt / 没装 VS 的 Windows 上直接双击的绿色包。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1
#   powershell ... -File tools/package.ps1 -SkipBuild      # 复用已有 release 产物
#
# 产物：
#   dist/Sonora-<版本>-win64.zip        解压即用（内含 sonora.exe + 全部依赖 + 说明 + 第三方声明）
#
# 为什么是"绿色包 + 另附安装脚本"而不是直接出一个 .exe 安装包：
# 安装包需要 Inno Setup，本机不一定有；绿色包是安装包的输入，先保证它单独可用。

param(
    [switch]$SkipBuild,
    [string]$QtRoot = "D:/Software/Qt/6.12.0/msvc2022_64"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

$windeployqt = Join-Path $QtRoot "bin/windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    throw "找不到 windeployqt：$windeployqt（用 -QtRoot 指定 Qt 安装目录）"
}

$portable = "C:/Users/123/AppData/Local/xmake-portable"
if (Test-Path "$portable/xmake.exe") { $env:PATH = "$portable;$env:PATH" }

# ---- 1. release 构建 ----
if (-not $SkipBuild) {
    Write-Host "== 配置 release"
    & xmake f -m release -a x64 -y --qt="$QtRoot"
    if ($LASTEXITCODE -ne 0) { throw "xmake f 失败" }
    Write-Host "== 构建 sonora"
    & xmake build -y sonora
    if ($LASTEXITCODE -ne 0) { throw "xmake build 失败" }
}

$exe = Join-Path $root "build/windows/x64/release/sonora.exe"
if (-not (Test-Path $exe)) { throw "没有 release 产物：$exe" }

# 版本号只有 xmake.lua 一处出处。直接读那一行，不另抄一份。
# （GUI 子系统的 exe 在 PowerShell 里 `& $exe --version` 拿不到 stdout，
#  所以不能拿 bin当版本源；下面用 Start-Process 重定向做一次一致性核对。）
$lua = Get-Content (Join-Path $root "xmake.lua") -Raw
if ($lua -notmatch 'SONORA_VERSION\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"') {
    throw "在 xmake.lua 里找不到 SONORA_VERSION"
}
$version = $Matches[1]
Write-Host "== 版本 $version"

# 核对：二进制自己说的版本要与 xmake.lua 一致（防止忘了重编或只改了一处）

# ---- 2. 收集运行时：Qt 部署 + 已有 FFmpeg DLL ----
Write-Host "== windeployqt"
# --qmldir 必须指到 QML 源码目录：它靠扫描 import 决定拷哪些 QML 模块，
# 漏了 QML 模块的症状是"窗口能开，某一页全白"。
& $windeployqt --release --no-translations --compiler-runtime `
    --qmldir "$root/presentation/qml" $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt 失败" }
# 版本一致性核对放在 windeployqt 之后：release 目录里此刻才有 Qt DLL，
# 否则进程根本起不来，stdout 是空的（踩过一次）。
# GUI 子系统的程序用 PowerShell 直接重定向拿不到 stdout，所以用 Process API 显式接管道。
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = "--version"
$psi.RedirectStandardOutput = $true
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$proc = [System.Diagnostics.Process]::Start($psi)
$verText = $proc.StandardOutput.ReadToEnd().Trim()
$proc.WaitForExit()
if ($verText -ne "Sonora Music $version") {
    throw "二进制版本与 xmake.lua 不一致：二进制报 '$verText'，xmake.lua 是 $version"
}

$releaseDir = Split-Path -Parent $exe

# ---- MSVC 运行库 ----
# windeployqt 的 --compiler-runtime 只在 VS 开发者命令行里才拿得到路径
# （靠 VCINSTALLDIR）。普通 shell 里它静默跳过，包看着没问题、干净机器上
# 一启动就"缺少 VCRUNTIME140.dll"。所以这里自己找 Redist 目录拷。
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$crtDir = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -property installationPath
    if ($vsPath) {
        $crtDir = Get-ChildItem -Path (Join-Path $vsPath "VC/Redist/MSVC") -Recurse -Directory `
                    -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
                  Where-Object { $_.FullName -like "*\x64\*" } |
                  Sort-Object FullName -Descending | Select-Object -First 1
    }
}
if ($crtDir) {
    Copy-Item (Join-Path $crtDir.FullName "*.dll") $releaseDir -Force
    Write-Host "== MSVC 运行库：$($crtDir.FullName)"
} else {
    Write-Warning "没找到 MSVC Redist 目录，包里不会带 vcruntime140.dll（干净机器上会缺 DLL）"
}

# 硬核对：少一个就是"装完打不开"或"打开但搜不了/播不了"。
$required = @(
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll", "Qt6Quick.dll", "Qt6QuickControls2.dll",
    "Qt6Multimedia.dll", "Qt6Network.dll",
    "platforms/qwindows.dll", "tls/qschannelbackend.dll",
    "avcodec-*.dll", "avformat-*.dll", "avutil-*.dll", "swresample-*.dll",
    "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll"
)
$missing = @()
foreach ($name in $required) {
    if ($name -like "*vc_redist*") { continue }   # 由 --compiler-runtime 装成 DLL，不是安装器
    if (-not (Get-ChildItem -Path (Join-Path $releaseDir $name) -ErrorAction SilentlyContinue)) {
        $missing += $name
    }
}
if ($missing.Count -gt 0) { throw "release 目录缺运行时：$($missing -join ', ')" }

# ---- 3. 组装发布目录 ----
$dist = Join-Path $root "dist"
$stage = Join-Path $dist "Sonora-$version-win64"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage | Out-Null

Write-Host "== 拷贝可执行文件与依赖"
Copy-Item $exe $stage
Copy-Item (Join-Path $releaseDir "*.dll") $stage -ErrorAction SilentlyContinue
foreach ($sub in @("platforms", "tls", "imageformats", "styles", "networkinformation")) {
    $from = Join-Path $releaseDir $sub
    if (Test-Path $from) { Copy-Item $from $stage -Recurse }
}
# QML 运行时（windeployqt 已经拷过，这里只兜底确认）
if (Test-Path (Join-Path $releaseDir "qml")) { Copy-Item (Join-Path $releaseDir "qml") $stage -Recurse }

foreach ($extra in @("THIRD-PARTY-NOTICES.md", "dist/README.txt")) {
    $from = Join-Path $root $extra
    if (Test-Path $from) { Copy-Item $from $stage }
}

# ---- 4. 打包 ----
$zip = Join-Path $dist "Sonora-$version-win64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Write-Host "== 压缩 $zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal

$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host ""
Write-Host "== 完成：$zip（${sizeMb} MB，$version）"
Write-Host "   冒烟：把 PATH 收成只剩 C:\Windows\system32，再双击 sonora.exe"
