# Copies the plugin into the FTL game folder. Run build.ps1 first.
#   .\install.ps1                 find FTL through Steam
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
if (-not $GameDir -or -not (Test-Path (Join-Path $GameDir "FTLGame.exe"))) {
    throw "Could not find FTLGame.exe. Pass the game folder with -GameDir."
}
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

$built = Join-Path $PSScriptRoot "build\dbghelp.dll"
if (-not (Test-Path $built)) { throw "build\dbghelp.dll not found; run build.ps1 first." }
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
if ($running) { Write-Host "FTL is running: restart it to load the plugin." }
