# 下载 Zig 工具链；若本机 WobbleDrag 已有 zig，则直接复用。
$ErrorActionPreference = 'Stop'
$tools = Join-Path $PSScriptRoot 'tools'
New-Item -ItemType Directory -Force -Path $tools | Out-Null

if (Get-ChildItem $tools -Recurse -Filter zig.exe -ErrorAction SilentlyContinue) {
    Write-Host '[OK] Zig 工具链已存在，跳过下载。'
    exit 0
}

$reuse = Join-Path $env:USERPROFILE 'WobbleDrag\tools'
$existing = Get-ChildItem $reuse -Recurse -Filter zig.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ($existing) {
    $srcDir = $existing.Directory.FullName
    $dest = Join-Path $tools $existing.Directory.Name
    Write-Host "[..] 复用已有工具链: $srcDir"
    cmd /c "mklink /J `"$dest`" `"$srcDir`"" | Out-Null
    if (Test-Path (Join-Path $dest 'zig.exe')) {
        Write-Host "[OK] 已链接 $dest"
        exit 0
    }
}

Write-Host '[..] 解析 Zig 最新稳定版下载地址 ...'
$json = (Invoke-WebRequest -Uri 'https://ziglang.org/download/index.json' -TimeoutSec 30 -UseBasicParsing).Content | ConvertFrom-Json
$ver  = $json.PSObject.Properties | Where-Object { $_.Name -match '^0\.\d+\.' } | Select-Object -First 1
if (-not $ver) { $ver = $json.PSObject.Properties | Where-Object { $_.Name -notmatch 'master' } | Select-Object -First 1 }
$win  = $ver.Value.PSObject.Properties | Where-Object { $_.Name -eq 'x86_64-windows' }
if (-not $win) { throw '未找到 Windows 版 Zig 下载项' }

$zip = Join-Path $tools 'zig.zip'
Write-Host "[..] 下载 Zig $($ver.Name) (x86_64-windows) ..."
Invoke-WebRequest -Uri $win.Value.tarball -OutFile $zip -TimeoutSec 600 -UseBasicParsing
Write-Host '[..] 解压 ...'
Expand-Archive -Path $zip -DestinationPath $tools -Force
Remove-Item $zip -Force
Write-Host "[OK] 工具链就绪: $(Get-ChildItem $tools -Recurse -Filter zig.exe | Select-Object -First 1 -ExpandProperty FullName)"
