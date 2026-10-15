param([switch]$ValidateOnly, [switch]$Voodoo)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath

function Assert-Hash([string]$Path, [string]$Expected) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path" }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) { throw "Missing or changed file: $Path" }
}

function Read-Config([string]$Path) {
    $settings = @{}
    $section = ''
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*\[(.+)\]\s*$') { $section = $Matches[1] }
        elseif ($line -match '^\s*([^=]+?)\s*=\s*(.*?)\s*$') {
            $settings["$section/$($Matches[1])"] = $Matches[2]
        }
    }
    return $settings
}

$label = if ($Voodoo) { '1996 3dfx' } else { '1996' }
try {
    $root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $pointer = Join-Path $root 'build\game\p5-200\latest-1996-vm.txt'
    if (!(Test-Path -LiteralPath $pointer)) { throw 'Prepare the 1996 VM first (tools/vm_1996.py play).' }
    $vm = [IO.Path]::GetFullPath((Get-Content -LiteralPath $pointer -Raw).Trim())
    # A worktree may link runtime\ to the main checkout's; compare real paths.
    $runtimeItem = Get-Item -LiteralPath (Join-Path $root 'runtime')
    $runtimePath = if ($runtimeItem.LinkType) { @($runtimeItem.Target)[0] } else { $runtimeItem.FullName }
    $runtime = [IO.Path]::GetFullPath($runtimePath).TrimEnd('\') + '\'
    if (!$vm.StartsWith($runtime, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Expected a private DOScraft runtime directory.'
    }
    $record = Get-Content -LiteralPath (Join-Path $vm 'build.json') -Raw | ConvertFrom-Json
    if ($record.vm_directory -ne $vm -or $record.target -ne 'p5-200-1996' -or
        $record.smoke -or $record.benchmark -or $record.probe) { throw 'Not a prepared 1996 player VM.' }
    $emulator = Join-Path $root 'build\deps\86box-doscraft-1996\86Box.exe'
    $roms = Join-Path $root 'build\deps\86box\roms'
    Assert-Hash $emulator $record.emulator_sha256
    Assert-Hash (Join-Path $root 'profiles\1996-p200.cfg') $record.profile_sha256
    foreach ($property in $record.rom_sha256.PSObject.Properties) {
        Assert-Hash (Join-Path $roms $property.Name) $property.Value
    }
    # The ISO carries D96REF, D96SOFT and D963DFX; the VM boots its embedded floppy.
    Assert-Hash (Join-Path $vm 'DOSCRA96.ISO') $record.iso_sha256
    Assert-Hash (Join-Path $vm 'boot.img') $record.boot_sha256
    if ((Get-Item -LiteralPath (Join-Path $vm 'boot.img')).Length -ne 2949120) {
        throw '1996 boot floppy is not a 2.88-MiB image.'
    }
    $disk = Join-Path $vm 'scratch-large.img'
    if (!(Test-Path -LiteralPath $disk) -or (Get-Item -LiteralPath $disk).Length -ne 206438400) {
        throw '1996 writable HDD is missing or has unexpected geometry.'
    }
    if (!(Test-Path -LiteralPath (Join-Path $vm 'nvr\p55t2p4.nvr') -PathType Leaf)) {
        throw '1996 CMOS is missing.'
    }
    $configName = if ($Voodoo) { '86box-3dfx.cfg' } else { '86box.cfg' }
    $settings = Read-Config (Join-Path $vm $configName)
    $required = @{
        'Machine/machine'='p55t2p4'; 'Machine/cpu_family'='pentium_p54c';
        'Machine/cpu_speed'='200000000'; 'Machine/cpu_multi'='3';
        'Machine/cpu_use_dynarec'='1'; 'Machine/fpu_type'='internal';
        'Machine/mem_size'='131072';
        'Video/gfxcard'='millennium'; 'Matrox Millennium/memory'='4';
        'Sound/sndcard'='sbawe64_gold';
        'Storage controllers/scsicard_1'='ncr53c875'; 'NCR 53c875 #1/bios'='1';
        'Hard disks/hdd_01_parameters'='63, 8, 800, 0, scsi';
        'Hard disks/hdd_01_scsi_location'='0:00';
        'Hard disks/hdd_01_speed'='st12550w_1996';
        'Hard disks/hdd_01_fn'='scratch-large.img';
        'Floppy and CD-ROM drives/fdd_01_fn'='boot.img';
        'Floppy and CD-ROM drives/fdd_01_type'='35_2ed';
        'Input devices/mouse_type'='msserial'
    }
    if ($Voodoo) {
        $required['Video/voodoo'] = '1'
        $required['3Dfx Voodoo Graphics/type'] = '0'
        $required['3Dfx Voodoo Graphics/framebuffer_memory'] = '2'
        $required['3Dfx Voodoo Graphics/texture_memory'] = '2'
    }
    foreach ($key in $required.Keys) {
        if ($settings[$key] -ne $required[$key]) { throw "Unexpected $label hardware: $key" }
    }
    if (!$Voodoo -and $settings['Video/voodoo'] -and $settings['Video/voodoo'] -ne '0') {
        throw 'The software launcher must not enable the Voodoo.'
    }
    if ($settings['Machine/cpu_override_interpreter'] -and
        $settings['Machine/cpu_override_interpreter'] -ne '0') { throw 'CPU timing override refused.' }
    # One machine: never run its writable disk under both configurations.
    $running = Get-CimInstance Win32_Process -Filter "Name = '86Box.exe'" |
        Where-Object { $_.CommandLine -like "*$vm*" }
    if ($running -and !$ValidateOnly) { throw 'This writable 1996 VM is already running.' }
    Write-Host "1996 DOScraft VM: $vm"
    Write-Host 'Pentium 200, 128 MiB, P/I-P55T2P4 (430HX), Matrox Millennium, AWE64 Gold, 53C875 SCSI.'
    if ($Voodoo) { Write-Host '3dfx Voodoo Graphics enabled: D963DFX starts.' }
    else { Write-Host 'No Voodoo: D963DFX declines and D96SOFT starts.' }
    if ($ValidateOnly) { Write-Host 'Validation only; nothing launched.'; exit 0 }
    Start-Process -FilePath $emulator -WorkingDirectory $vm -ArgumentList @(
        '-P', "`"$vm`"", '-C', $configName, '-R', "`"$roms`"")
    exit 0
} catch {
    Write-Host "$label DOScraft launch refused: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
