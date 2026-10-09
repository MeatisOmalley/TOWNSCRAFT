# Shared, late-1989 Towns hardware profile for the numbered ISO launchers.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$IsoPath,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$diskBytes = 200000000L

try {
    $gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $emulator = Join-Path $gameRoot 'build\emulator\main_cui\Release\Tsugaru_CUI.exe'
    $rom = Join-Path $gameRoot 'build\play\STUBROM'
    foreach ($requiredFile in @($emulator, $IsoPath)) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "Required file not found: $requiredFile"
        }
    }
    if (-not (Test-Path -LiteralPath $rom -PathType Container)) {
        throw "ROM directory not found: $rom"
    }
    $iso = (Resolve-Path -LiteralPath $IsoPath).Path
    $diskDirectory = Join-Path ([IO.Path]::GetDirectoryName($iso)) 'runtime_1989'
    $hardDisk = Join-Path $diskDirectory ([IO.Path]::GetFileNameWithoutExtension($iso) + '.HDD0.h0')

    # 2F is Tsugaru's shared machine ID for the 1F/2F/1H/2H generation.
    # Keep normal device timings and real-time pacing, not FASTSCSI/NOWAIT.
    $emulatorArguments = @(
        $rom, '-CD', $iso, '-TOWNSTYPE', '2F', '-FREQ', '16',
        '-MEMSIZE', '8', '-USEFPU', '-CDSPEED', '1',
        '-HD0', $hardDisk, '-NORMALSCSI', '-NORMALFD',
        '-DIFFMOUSE', '-DONTAUTOSAVECMOS', '-YESWAIT', '-AUTOSCALE', '-MAXIMIZE'
    )

    if ($ValidateOnly) {
        [pscustomobject]@{
            Emulator = $emulator
            WorkingDirectory = $gameRoot
            Iso = $iso
            HardDisk = $hardDisk
            NewDiskBytes = $diskBytes
            Arguments = $emulatorArguments
        } | ConvertTo-Json -Depth 3
        exit 0
    }

    if (Test-Path -LiteralPath $hardDisk) {
        if (-not (Test-Path -LiteralPath $hardDisk -PathType Leaf)) {
            throw "Hard disk image path is not a file: $hardDisk"
        }
        if ((Get-Item -LiteralPath $hardDisk).Length -ne $diskBytes) {
            throw "Existing HDD image is not 200 MB; it has been left unchanged: $hardDisk"
        }
    } else {
        New-Item -ItemType Directory -Path $diskDirectory -Force | Out-Null
        # CreateNew refuses to overwrite a disk, including a concurrent creation.
        # This is an unformatted raw SCSI image, not an installed Towns OS disk.
        $diskStream = [IO.File]::Open($hardDisk, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
            $diskStream.SetLength($diskBytes)
        } finally {
            $diskStream.Dispose()
        }
    }

    Push-Location -LiteralPath $gameRoot
    try {
        & $emulator @emulatorArguments
        $emulatorExit = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    exit $emulatorExit
} catch {
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    exit 1
}
