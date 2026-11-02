# Packs the built plugin into build\ftl-speed-<version>.zip, the file a release
# ships. Run build.ps1 first. The version comes from PLUGIN_NAME in
# src\ftl_speed.c. Also writes build\ftl-speed-<version>.zip.sha256 and
# build\release-notes.md (the text of the GitHub release).
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$out = Join-Path $root "build"
$dll = Join-Path $out "dbghelp.dll"
if (-not (Test-Path $dll)) { throw "build\dbghelp.dll not found; run build.ps1 first." }

$src = [IO.File]::ReadAllText((Join-Path $root "src\ftl_speed.c"))
if ($src -cnotmatch '#define PLUGIN_NAME "FTL Speed plugin ([0-9][0-9A-Za-z.-]*)"') {
    throw "no version found in PLUGIN_NAME in src\ftl_speed.c"
}
$version = $Matches[1]
$name = "ftl-speed-$version"

$stage = Join-Path $out "package"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item $dll $stage

# Text files get Windows line endings so they read right in Notepad.
$utf8 = New-Object Text.UTF8Encoding $false
function Write-Text([string]$from, [string]$to, [hashtable]$vars = @{}) {
    $text = [IO.File]::ReadAllText((Join-Path $root $from)) -replace "`r?`n", "`r`n"
    foreach ($k in $vars.Keys) { $text = $text.Replace("{$k}", $vars[$k]) }
    [IO.File]::WriteAllText($to, $text, $utf8)
}
Write-Text "ftl_speed.ini" (Join-Path $stage "ftl_speed.ini")
Write-Text "install.ps1" (Join-Path $stage "install.ps1")
Write-Text "release\Install.bat" (Join-Path $stage "Install.bat")
Write-Text "release\Uninstall.bat" (Join-Path $stage "Uninstall.bat")
Write-Text "release\README.txt" (Join-Path $stage "README.txt") @{ VERSION = $version }
Write-Text "LICENSE" (Join-Path $stage "LICENSE.txt")

$zip = Join-Path $out "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$zip.sha256", "$hash  $name.zip`n", $utf8)
Write-Text "release\notes.md" (Join-Path $out "release-notes.md") @{ VERSION = $version; SHA256 = $hash }

Write-Host "Packed $zip"
Get-ChildItem $stage | ForEach-Object { Write-Host "  $($_.Name)" }
Write-Host "SHA-256 $hash"
if ($env:GITHUB_OUTPUT) { [IO.File]::AppendAllText($env:GITHUB_OUTPUT, "version=$version`n", $utf8) }
