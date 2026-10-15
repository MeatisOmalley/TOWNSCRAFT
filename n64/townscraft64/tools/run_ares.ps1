# Run a ROM in the pinned ares with a private settings file.
# Defaults to the 4 MB base console; -ExpansionPak gives 8 MB.
# -Randnet uses the ares fork with the Randnet keyboard (tools/build_randnet_ares.py)
# instead of stock v148. With it, -Keyboard <port> and -Mouse <port> connect a
# Randnet keyboard / N64 mouse at load (other ports get gamepads); a keyboard
# connected this way starts with keyboard capture on.
# Usage: powershell -File tools/run_ares.ps1 [-Rom <file.z64>] [-ExpansionPak]
#                     [-Randnet [-Keyboard <1-4>] [-Mouse <1-4>]]
param([string]$Rom, [switch]$ExpansionPak, [switch]$Randnet,
      [ValidateRange(0, 4)][int]$Keyboard = 0, [ValidateRange(0, 4)][int]$Mouse = 0)
$root = Split-Path -Parent $PSScriptRoot
if (-not $Rom) { $Rom = Join-Path $root 'townscraft64.z64' }
if (($Keyboard -or $Mouse) -and -not $Randnet) { throw '-Keyboard and -Mouse need -Randnet' }
if ($Keyboard -and $Keyboard -eq $Mouse) { throw '-Keyboard and -Mouse must use different ports' }
if ($Randnet) {
    $ares = Join-Path $root 'build\deps\ares-randnet\ares.exe'
    if (-not (Test-Path $ares)) { throw 'ares fork missing: run python tools/build_randnet_ares.py' }
    $runtime = Join-Path $root 'build\ares-randnet-runtime'
} else {
    $ares = Join-Path $root 'build\deps\ares\ares-v148\ares.exe'
    if (-not (Test-Path $ares)) { throw 'ares missing: run python tools/fetch_dependencies.py' }
    $runtime = Join-Path $root 'build\ares-runtime'
}
if (-not (Test-Path $Rom)) { throw "ROM not found: $Rom (run python tools/build.py)" }
New-Item -ItemType Directory -Force $runtime | Out-Null
$pak = if ($ExpansionPak) { 'true' } else { 'false' }
$argv = @('--system', '"Nintendo 64"', '--no-file-prompt',
          '--settings-file', "`"$runtime\settings.bml`"",
          '--setting', "Nintendo64/ExpansionPak=$pak")
if ($Randnet) {
    foreach ($port in 1..4) {
        $device = if ($port -eq $Keyboard) { 'Randnet Keyboard' } elseif ($port -eq $Mouse) { 'Mouse' } else { 'Gamepad' }
        $argv += @('--setting', "`"Nintendo64/ControllerPort$port=$device`"")
    }
}
$argv += "`"$((Resolve-Path $Rom).Path)`""
$proc = Start-Process -FilePath $ares -ArgumentList $argv -PassThru
$which = if ($Randnet) { "ares-randnet (keyboard port $Keyboard, mouse port $Mouse)" } else { 'ares v148' }
"$which pid $($proc.Id), Expansion Pak $pak"
