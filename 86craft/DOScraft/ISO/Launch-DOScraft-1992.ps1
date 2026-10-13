param([switch]$ValidateOnly)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath
. (Join-Path $PSScriptRoot 'Sync-Shared-Release.ps1')

function Assert-Hash([string]$Path, [string]$Expected) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path" }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) { throw "Missing or changed file: $Path" }
}

try {
    $root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $pointer = Join-Path $root 'build\game\486dx25\latest-1992-vm.txt'
    if (!(Test-Path -LiteralPath $pointer)) { throw 'Prepare the separate 1992 VM first.' }
    $vm = [IO.Path]::GetFullPath((Get-Content -LiteralPath $pointer -Raw).Trim())
    $runtime = [IO.Path]::GetFullPath((Join-Path $root 'runtime')) + '\'
    if (!$vm.StartsWith($runtime, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Expected a private DOScraft runtime directory.'
    }
    $record = Get-Content -LiteralPath (Join-Path $vm 'build.json') -Raw | ConvertFrom-Json
    if ($record.vm_directory -ne $vm -or $record.target -ne 'dx2-80-1992' -or
        $record.smoke -or $record.benchmark) { throw 'Not a prepared 1992 player VM.' }
    $publishedPointer = Join-Path $root 'build\game\486dx25\latest-play-vm.txt'
    if (!(Test-Path -LiteralPath $publishedPointer -PathType Leaf)) {
        throw 'No published shared ISO release was found.'
    }
    $publishedVm = [IO.Path]::GetFullPath((Get-Content -LiteralPath $publishedPointer -Raw).Trim())
    if (!$publishedVm.StartsWith($runtime, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Expected a private published DOScraft runtime directory.'
    }
    $published = Get-Content -LiteralPath (Join-Path $publishedVm 'build.json') -Raw | ConvertFrom-Json
    if ($published.vm_directory -ne $publishedVm -or $published.target -ne '486dx25' -or
        $published.smoke -or $published.benchmark -or !$published.exe_sha256) {
        throw 'The shared ISO does not have a valid published release.'
    }
    $sharedIso = Join-Path $PSScriptRoot 'DOSCRAFT.ISO'
    $publishedBoot = Join-Path $publishedVm 'boot.img'
    Assert-Hash $sharedIso $published.iso_sha256
    Assert-Hash $publishedBoot $published.boot_sha256
    $emulator = Join-Path $root 'build\deps\86box-doscraft-1992\86Box.exe'
    $roms = Join-Path $root 'build\deps\86box\roms'
    Assert-Hash $emulator $record.emulator_sha256
    foreach ($property in $record.rom_sha256.PSObject.Properties) {
        Assert-Hash (Join-Path $roms $property.Name) $property.Value
    }
    $largeWorld = $record.large_world -eq $true
    $diskName = if ($largeWorld) { 'scratch-large.img' } else { 'scratch.img' }
    $diskBytes = if ($largeWorld) { 206438400 } else { 21411840 }
    $disk = Join-Path $vm $diskName
    if (!(Test-Path -LiteralPath $disk) -or (Get-Item -LiteralPath $disk).Length -ne $diskBytes) {
        throw '1992 writable HDD is missing or has unexpected geometry.'
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
        'Machine/machine'='av4'; 'Machine/cpu_family'='i486dx2';
        'Machine/cpu_speed'='80000000'; 'Machine/cpu_use_dynarec'='0';
        'Machine/fpu_type'='internal'; 'Machine/mem_size'='16384';
        'Video/gfxcard'='et4000w32_vlb'; 'Tseng Labs ET4000/w32 VLB/memory'='1';
        'Storage controllers/scsicard_1'='aha154xc';
        'Adaptec AHA-154xC #1/bios_addr'='C8000';
        'Hard disks/hdd_01_fn'=$diskName;
        'Hard disks/hdd_01_parameters'=$(if ($largeWorld) { '63, 8, 800, 0, scsi' } else { '17, 4, 615, 0, scsi' });
        'Hard disks/hdd_01_scsi_location'='0:02';
        'Hard disks/hdd_01_speed'='quantum_1050s_1992';
        'Floppy and CD-ROM drives/fdd_01_fn'='boot.img';
        'Floppy and CD-ROM drives/fdd_01_type'='35_2hd';
        'Input devices/mouse_type'='msserial'
    }
    foreach ($key in $required.Keys) {
        if ($settings[$key] -ne $required[$key]) { throw "Unexpected 1992 hardware: $key" }
    }
    if ($settings['Machine/cpu_override_interpreter'] -and
        $settings['Machine/cpu_override_interpreter'] -ne '0') { throw 'CPU timing override refused.' }
    $running = Get-CimInstance Win32_Process -Filter "Name = '86Box.exe'" |
        Where-Object { $_.ExecutablePath -eq $emulator -and $_.CommandLine -like "*$vm*" }
    if ($running -and !$ValidateOnly) { throw 'This writable 1992 VM is already running.' }
    if ($ValidateOnly) {
        Write-Host 'Shared ISO and published boot image are valid; any pending boot refresh occurs on the next launch.'
    } elseif (Sync-SharedRelease -Vm $vm -VmRecord $record -PublishedBoot $publishedBoot `
        -PublishedRecord $published -SharedIso $sharedIso) {
        Write-Host 'Updated the 1992 boot floppy from the shared ISO release; HDD and CMOS were preserved.'
    }
    if (!$ValidateOnly) { Assert-Hash (Join-Path $vm 'boot.img') $published.boot_sha256 }
    Write-Host "1992 DOScraft VM: $vm"
    Write-Host 'DX2/80 (40 MHz bus), 16 MiB, ET4000/W32 VLB, AHA-1542C ISA SCSI.'
    if ($largeWorld) { Write-Host '1024-block prebaked HDD world; Quantum ProDrive 1050S timing.' }
    if ($ValidateOnly) { exit 0 }
    Start-Process -FilePath $emulator -WorkingDirectory $vm -ArgumentList @('-P', "`"$vm`"", '-R', "`"$roms`"")
    exit 0
} catch {
    Write-Host "1992 DOScraft launch refused: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
