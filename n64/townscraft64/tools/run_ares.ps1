# Run a ROM in the pinned ares with a private settings file.
# Defaults to the 4 MB base console; -ExpansionPak gives 8 MB.
# Usage: powershell -File tools/run_ares.ps1 [-Rom <file.z64>] [-ExpansionPak]
param([string]$Rom, [switch]$ExpansionPak)
$root = Split-Path -Parent $PSScriptRoot
if (-not $Rom) { $Rom = Join-Path $root 'townscraft64.z64' }
$ares = Join-Path $root 'build\deps\ares\ares-v148\ares.exe'
if (-not (Test-Path $ares)) { throw 'ares missing: run python tools/fetch_dependencies.py' }
if (-not (Test-Path $Rom)) { throw "ROM not found: $Rom (run python tools/build.py)" }
$runtime = Join-Path $root 'build\ares-runtime'
New-Item -ItemType Directory -Force $runtime | Out-Null
$pak = if ($ExpansionPak) { 'true' } else { 'false' }
# Saves (Controller Pak, FlashRAM, ...) go under build/, not beside the ROM.
New-Item -ItemType Directory -Force "$runtime\saves" | Out-Null
$argv = @('--system', '"Nintendo 64"', '--no-file-prompt',
          '--settings-file', "`"$runtime\settings.bml`"",
          # Forward slashes: a trailing backslash would escape the closing quote.
          '--setting', "`"Paths/Saves=$(($runtime -replace '\\', '/'))/saves/`"",
          '--setting', "Nintendo64/ExpansionPak=$pak", "`"$((Resolve-Path $Rom).Path)`"")
$proc = Start-Process -FilePath $ares -ArgumentList $argv -PassThru
"ares pid $($proc.Id), Expansion Pak $pak"
