param([switch]$Deploy)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products "*" -property installationPath
    $cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
& $cmake -S $root -B (Join-Path $root "build") -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
& $cmake --build (Join-Path $root "build") --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed." }
if ($Deploy) {
    $mods = Join-Path (Split-Path -Parent $root) "fh2-mods"
    if (Get-Process -Name "azimuth" -ErrorAction SilentlyContinue) { throw "Close the game first." }
    Copy-Item (Join-Path $root "dist\VirtualDashboard.dll") $mods -Force
}
Write-Host "Done."
