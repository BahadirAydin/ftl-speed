# Copies the plugin into the FTL game folder. Works from the release zip
# (dbghelp.dll next to this script) or from the source tree after build.ps1.
#   .\install.ps1                 find FTL through Steam, or ask for the folder
#   .\install.ps1 -GameDir <dir>  or point at the folder with FTLGame.exe
#   -Uninstall                    remove the plugin again
param(
    [string]$GameDir,
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"
$marker = "FTL Speed plugin"

function Find-FtlDir {
    $steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { return $null }
    $libraries = @($steam)
    $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
    if (Test-Path $vdf) {
        $libraries += Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' }
    }
    foreach ($lib in $libraries) {
        $dir = Join-Path $lib "steamapps\common\FTL Faster Than Light"
        if (Test-Path (Join-Path $dir "FTLGame.exe")) { return $dir }
    }
    return $null
}

function Test-IsPlugin([string]$path) {
    (Test-Path $path) -and ([IO.File]::ReadAllText($path, [Text.Encoding]::ASCII).Contains($marker))
}

if (-not $GameDir) { $GameDir = Find-FtlDir }
if (-not $GameDir) {
    Write-Host "Could not find FTL through Steam."
    try { $GameDir = Read-Host "Paste the FTL game folder (the one with FTLGame.exe)" } catch { }
    $GameDir = "$GameDir".Trim().Trim('"')
    if ($GameDir -like "*FTLGame.exe") { $GameDir = Split-Path -Parent $GameDir }
}
if (-not $GameDir -or -not (Test-Path (Join-Path $GameDir "FTLGame.exe"))) {
    throw "Could not find FTLGame.exe. Pass the game folder with -GameDir."
}
$GameDir = (Resolve-Path -LiteralPath $GameDir).ProviderPath
$dll = Join-Path $GameDir "dbghelp.dll"
$ini = Join-Path $GameDir "ftl_speed.ini"
$running = Get-Process FTLGame -ErrorAction SilentlyContinue

if ($Uninstall) {
    if ($running) { throw "FTL is running; close it first (the plugin file is in use)." }
    if (Test-Path $dll) {
        if (-not (Test-IsPlugin $dll)) { throw "$dll is not this plugin; leaving it alone." }
        Remove-Item $dll
    }
    Remove-Item (Join-Path $GameDir "ftl_speed.log") -ErrorAction SilentlyContinue
    if (Test-Path $ini) { Remove-Item $ini }
    Write-Host "FTL Speed removed from $GameDir"
    return
}

# release zip: next to this script; source tree: build\ after build.ps1
$built = @((Join-Path $PSScriptRoot "dbghelp.dll"), (Join-Path $PSScriptRoot "build\dbghelp.dll")) |
    Where-Object { Test-IsPlugin $_ } | Select-Object -First 1
if (-not $built) { throw "dbghelp.dll not found next to install.ps1 (or in build\; run build.ps1 first)." }
if ($built -eq $dll) {
    Write-Host "FTL Speed is already in the game folder ($GameDir). Start FTL normally."
    return
}
if ((Test-Path $dll) -and -not (Test-IsPlugin $dll)) {
    throw "$dll already exists and is not this plugin (another mod?). Not overwriting it."
}
if ($running -and (Test-Path $dll)) { throw "FTL is running with the plugin loaded; close it first to update." }
Copy-Item $built $dll -Force
if (Test-Path $ini) {
    Write-Host "Kept your existing ftl_speed.ini"
} else {
    Copy-Item (Join-Path $PSScriptRoot "ftl_speed.ini") $ini
}
Write-Host "FTL Speed installed to $GameDir"
Write-Host "Start FTL normally. Keys and speeds are set in $ini."
if ($running) { Write-Host "FTL is running: restart it to load the plugin." }
