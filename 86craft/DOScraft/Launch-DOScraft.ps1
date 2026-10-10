param([switch]$ValidateOnly)
$ErrorActionPreference = 'Stop'
# A parent PowerShell 7 process can pass a module path lacking Windows
# PowerShell's own modules to the .cmd child. This is process-local only.
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath

function Assert-Hash([string]$Path, [string]$Expected) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing build dependency: $Path"
    }
    $stream = [IO.File]::OpenRead($Path)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $digest = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hasher.Dispose() }
    if ($digest -ne $Expected) {
        throw "Missing or changed build dependency: $Path"
    }
}

try {
    $taskRoot = $PSScriptRoot
    $pointer = Join-Path $taskRoot 'build\game\486dx25\latest-play-vm.txt'
    if (!(Test-Path -LiteralPath $pointer)) {
        throw 'No playable ISO has been prepared. Run tools/build_game.py, then tools/game_media.py.'
    }
    $vm = [IO.Path]::GetFullPath((Get-Content -LiteralPath $pointer -Raw).Trim())
    $runtimeRoot = [IO.Path]::GetFullPath((Join-Path $taskRoot 'runtime')) + '\'
    if (!$vm.StartsWith($runtimeRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Game VM must be a private DOScraft runtime directory.'
    }
    $record = Get-Content -LiteralPath (Join-Path $vm 'build.json') -Raw | ConvertFrom-Json
    if ($record.smoke -or $record.target -ne '486dx25' -or $record.vm_directory -ne $vm) {
        throw 'Expected the prepared playable 486 ISO, not diagnostic media.'
    }
    $emulator = Join-Path $taskRoot 'build\deps\86box\86Box.exe'
    $running = Get-CimInstance Win32_Process -Filter "Name = '86Box.exe'" |
        Where-Object { $_.ExecutablePath -eq $emulator -and $_.CommandLine -like "*$vm*" }
    if ($running -and !$ValidateOnly) {
        throw 'This game VM is already running. Do not open its writable HDD twice.'
    }
    Assert-Hash (Join-Path $vm 'DOSCRAFT.ISO') $record.iso_sha256
    Assert-Hash (Join-Path $vm 'boot.img') $record.boot_sha256
    $roms = Join-Path $taskRoot 'build\deps\86box\roms'
    Assert-Hash $emulator $record.emulator_sha256
    foreach ($property in $record.rom_sha256.PSObject.Properties) {
        Assert-Hash (Join-Path $roms $property.Name) $property.Value
    }
    # Existing HDD content is intentionally NOT checksummed or regenerated:
    # saves/terrain change during normal play and must survive every launch.
    $disk = Join-Path $vm 'scratch.img'
    if (!(Test-Path -LiteralPath $disk) -or (Get-Item -LiteralPath $disk).Length -ne 21411840) {
        throw 'Prepared writable game HDD is missing or has unexpected geometry.'
    }
    $settings = @{}
    $section = ''
    foreach ($line in Get-Content -LiteralPath (Join-Path $vm '86box.cfg')) {
        if ($line -match '^\s*\[(.+)\]\s*$') { $section = $Matches[1] }
        elseif ($line -match '^\s*([^=]+?)\s*=\s*(.*?)\s*$') {
            $settings["$section/$($Matches[1])"] = $Matches[2]
        }
    }
    $required = @{
        'Machine/machine'='isa486'; 'Machine/cpu_family'='i486dx';
        'Machine/cpu_speed'='25000000'; 'Machine/cpu_use_dynarec'='0';
        'Machine/fpu_type'='internal'; 'Machine/mem_size'='16384';
        'Video/gfxcard'='et4000ax'; 'Tseng Labs ET4000AX (ISA)/bios'='v8_06';
        'Tseng Labs ET4000AX (ISA)/memory'='1024';
        'Storage controllers/hdc_1'='esdi_at';
        'Hard disks/hdd_01_fn'='scratch.img';
        'Hard disks/hdd_01_parameters'='17, 4, 615, 0, esdi';
        'Hard disks/hdd_01_speed'='1989_3500rpm';
        'Floppy and CD-ROM drives/fdd_01_fn'='boot.img';
        'Floppy and CD-ROM drives/fdd_01_type'='35_2hd';
        'Input devices/mouse_type'='msserial'
    }
    foreach ($key in $required.Keys) {
        if ($settings[$key] -ne $required[$key]) { throw "Unexpected hardware: $key" }
    }
    if ($settings['Machine/cpu_override_interpreter'] -and
        $settings['Machine/cpu_override_interpreter'] -ne '0') { throw 'CPU timing override refused.' }
    if ($settings['Ports (COM & LPT)/serial1_enabled'] -eq '0') { throw 'COM1 is disabled.' }
    foreach ($pair in @(@('Microsoft Serial Mouse/port','0'), @('Microsoft Serial Mouse/buttons','2'))) {
        if ($settings.ContainsKey($pair[0]) -and $settings[$pair[0]] -ne $pair[1]) {
            throw "Unexpected mouse setting: $($pair[0])"
        }
    }
    foreach ($key in $settings.Keys) {
        if (($key -match '/serial\d+_device$' -and $settings[$key] -ne 'none') -or
            ($key -match '/serial\d+_passthrough_enabled$' -and $settings[$key] -ne '0')) {
            throw 'External serial passthrough refused.'
        }
    }
    Write-Host "DOScraft ISO: $(Join-Path $vm 'DOSCRAFT.ISO')"
    Write-Host '486DX/25, 16 MiB, ET4000. Audio deferred. Existing saves are preserved.'
    if ($ValidateOnly) { exit 0 }
    Start-Process -FilePath $emulator -WorkingDirectory $vm -ArgumentList @('-P', "`"$vm`"", '-R', "`"$roms`"")
    exit 0
} catch {
    Write-Host "DOScraft launch refused: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
