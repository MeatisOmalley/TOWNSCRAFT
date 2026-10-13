param([switch]$ValidateOnly, [string]$IsoPath)
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
    $taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
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
    $largeWorld = $record.large_world -eq $true
    if ($record.smoke -or $record.benchmark -or $record.target -ne '486dx25' -or $record.vm_directory -ne $vm) {
        throw 'Expected the prepared playable 486 ISO, not diagnostic media.'
    }
    # Keep the published 33 MHz player VM launchable while its 40 MHz
    # replacement is tested on private media.
    $emulator = Join-Path $taskRoot 'build\deps\86box-doscraft\86Box.exe'
    $cpuSpeed = '33333333'
    $cpuLabel = '486DX/33'
    $memory = '16384'
    if ($record.emulator_sha256 -eq 'cd80d8691bca79fadf40c3c453479d5a377731422e1e0eaacb07adc286717c12') {
        $emulator = Join-Path $taskRoot 'build\deps\86box-doscraft-40\86Box.exe'
        $cpuSpeed = '40000000'
        $cpuLabel = '486DX/40 overclock'
    }
    if ($record.hardware_era -eq '1990-06-30') {
        $emulator = Join-Path $taskRoot 'build\deps\86box-doscraft-1990\86Box.exe'
        $cpuSpeed = '40000000'
        $cpuLabel = '486DX/40 overclock'
        $memory = '8192'
    }
    $running = Get-CimInstance Win32_Process -Filter "Name = '86Box.exe'" |
        Where-Object { $_.ExecutablePath -eq $emulator -and $_.CommandLine -like "*$vm*" }
    if ($running -and !$ValidateOnly) {
        throw 'This game VM is already running. Do not open its writable HDD twice.'
    }
    if (!$IsoPath) { $IsoPath = Join-Path $PSScriptRoot 'DOSCRAFT.ISO' }
    $IsoPath = [IO.Path]::GetFullPath($IsoPath)
    Assert-Hash $IsoPath $record.iso_sha256
    Assert-Hash (Join-Path $vm 'boot.img') $record.boot_sha256
    $roms = Join-Path $taskRoot 'build\deps\86box\roms'
    Assert-Hash $emulator $record.emulator_sha256
    foreach ($property in $record.rom_sha256.PSObject.Properties) {
        Assert-Hash (Join-Path $roms $property.Name) $property.Value
    }
    # Existing HDD content is intentionally NOT checksummed or regenerated:
    # saves/terrain change during normal play and must survive every launch.
    $disk = Join-Path $vm $(if ($largeWorld) { 'scratch-large.img' } else { 'scratch.img' })
    $diskBytes = if ($largeWorld) { 206438400 } else { 21411840 }
    if (!(Test-Path -LiteralPath $disk) -or (Get-Item -LiteralPath $disk).Length -ne $diskBytes) {
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
        'Machine/cpu_speed'=$cpuSpeed; 'Machine/cpu_use_dynarec'='0';
        'Machine/fpu_type'='internal'; 'Machine/mem_size'=$memory;
        'Video/gfxcard'='et4000ax'; 'Tseng Labs ET4000AX (ISA)/bios'='v8_06';
        'Tseng Labs ET4000AX (ISA)/memory'='1024';
        'Hard disks/hdd_01_fn'=$(if ($largeWorld) { 'scratch-large.img' } else { 'scratch.img' });
        'Floppy and CD-ROM drives/fdd_01_fn'='boot.img';
        'Floppy and CD-ROM drives/fdd_01_type'='35_2hd';
        'Input devices/mouse_type'='msserial'
    }
    if ($record.hardware_era -eq '1990-06-30') {
        $required['Storage controllers/scsicard_1'] = 'aha154xa'
        $required['Adaptec AHA-154xA #1/base'] = '330'
        $required['Adaptec AHA-154xA #1/bios_addr'] = 'C8000'
        $required['Hard disks/hdd_01_parameters'] = $(if ($largeWorld) { '63, 8, 800, 0, scsi' } else { '17, 4, 615, 0, scsi' })
        $required['Hard disks/hdd_01_scsi_location'] = '0:00'
        $required['Hard disks/hdd_01_speed'] = 'st4385n_1990'
    } else {
        $required['Storage controllers/hdc_1'] = 'esdi_at'
        $required['Hard disks/hdd_01_parameters'] = '17, 4, 615, 0, esdi'
        $required['Hard disks/hdd_01_speed'] = '1989_3500rpm'
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
    Write-Host "DOScraft ISO: $IsoPath"
    if ($record.hardware_era -eq '1990-06-30') {
        $worldLabel = if ($largeWorld) { '1024-block prebaked HDD world' } else { 'existing world' }
        Write-Host "$cpuLabel, 8 MiB, ET4000AX, AHA-1542A SCSI and ST4385N timing; $worldLabel."
    } else {
        Write-Host "$cpuLabel, 16 MiB, patched 16-bit ET4000. Audio deferred. Existing saves are preserved."
    }
    if ($ValidateOnly) { exit 0 }
    Start-Process -FilePath $emulator -WorkingDirectory $vm -ArgumentList @('-P', "`"$vm`"", '-R', "`"$roms`"")
    exit 0
} catch {
    Write-Host "DOScraft launch refused: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
