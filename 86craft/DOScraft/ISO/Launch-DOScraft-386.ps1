param([switch]$ValidateOnly)
$ErrorActionPreference = 'Stop'

function Assert-Hash([string]$Path, [string]$Expected) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Expected) {
        throw "Missing or changed file: $Path"
    }
}

try {
    $root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $pointer = Join-Path $root 'build\game\386sx25\latest-play-vm.txt'
    $vm = [IO.Path]::GetFullPath((Get-Content -LiteralPath $pointer -Raw).Trim())
    $runtime = [IO.Path]::GetFullPath((Join-Path $root 'runtime')) + '\'
    if (!$vm.StartsWith($runtime, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Expected a private DOScraft runtime directory.'
    }
    $record = Get-Content -LiteralPath (Join-Path $vm 'build.json') -Raw | ConvertFrom-Json
    if ($record.vm_directory -ne $vm -or $record.target -ne '386sx25-family-1990' -or
        $record.smoke -or $record.benchmark) { throw 'Not a prepared 386 player VM.' }
    $emulator = Join-Path $root 'build\deps\86box-doscraft-386\86Box.exe'
    $roms = Join-Path $root 'build\deps\86box\roms'
    Assert-Hash $emulator $record.emulator_sha256
    Assert-Hash (Join-Path $vm 'boot.img') $record.boot_sha256
    Assert-Hash (Join-Path $vm 'DOSCRAFT.ISO') $record.iso_sha256
    foreach ($property in $record.rom_sha256.PSObject.Properties) {
        Assert-Hash (Join-Path $roms $property.Name) $property.Value
    }
    $disk = Join-Path $vm 'hdd.img'
    if (!(Test-Path -LiteralPath $disk) -or (Get-Item -LiteralPath $disk).Length -ne 42519040) {
        throw '386 writable HDD is missing or has unexpected geometry.'
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
        'Machine/machine'='dtk386'; 'Machine/cpu_family'='i386sx';
        'Machine/cpu_speed'='25000000'; 'Machine/mem_size'='4096';
        'Video/gfxcard'='tvga8900'; 'Trident TVGA 8900/bios'='tvga8900b';
        'Trident TVGA 8900/memory'='512';
        'Storage controllers/hdc_1'='ide_isa';
        'Hard disks/hdd_01_parameters'='17, 5, 977, 0, ide';
        'Hard disks/hdd_01_ide_channel'='0:0'; 'Hard disks/hdd_01_speed'='CP3044';
        'Hard disks/hdd_01_fn'='hdd.img';
        'Floppy and CD-ROM drives/fdd_01_fn'='boot.img';
        'Floppy and CD-ROM drives/fdd_01_type'='35_2hd';
        'Input devices/mouse_type'='msserial'
    }
    foreach ($key in $required.Keys) {
        if ($settings[$key] -ne $required[$key]) { throw "Unexpected 386 hardware: $key" }
    }
    foreach ($key in @('Machine/cpu_use_dynarec','Machine/cpu_override_interpreter')) {
        if ($settings[$key] -and $settings[$key] -ne '0') { throw "CPU timing override refused: $key" }
    }
    $running = Get-CimInstance Win32_Process -Filter "Name = '86Box.exe'" |
        Where-Object { $_.ExecutablePath -eq $emulator -and $_.CommandLine -like "*$vm*" }
    if ($running -and !$ValidateOnly) { throw 'This writable 386 VM is already running.' }
    Write-Host "386 DOScraft VM: $vm"
    Write-Host '386SX-25, 4 MiB, Trident VGA, 40 MB IDE; compact 1024-block world.'
    if ($ValidateOnly) { exit 0 }
    $launch = New-Object System.Diagnostics.ProcessStartInfo
    $launch.FileName = $emulator
    $launch.WorkingDirectory = $vm
    $launch.Arguments = '-P "' + $vm + '" -R "' + $roms + '"'
    $launch.UseShellExecute = $true
    [void][System.Diagnostics.Process]::Start($launch)
    exit 0
} catch {
    Write-Host "386 DOScraft launch refused: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
