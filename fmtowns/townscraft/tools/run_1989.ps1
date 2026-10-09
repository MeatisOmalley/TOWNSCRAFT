# Shared, late-1989 Towns hardware profile for the numbered ISO launchers.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$IsoPath,
    [switch]$ValidateOnly,
    [switch]$PrepareDiskOnly
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

    # Claim only a new/all-zero disk or our explicitly marked scratch disk.
    # A zero boot sector alone is not proof that the rest of a disk is empty.
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
public static class TownscraftTerrainDisk {
    public static void Initialize(string path) {
        byte[] header = new byte[512];
        Encoding.ASCII.GetBytes("TSC-TERRAIN-TEMP").CopyTo(header, 0);
        BitConverter.GetBytes((UInt32)1).CopyTo(header, 16);
        BitConverter.GetBytes((UInt32)256).CopyTo(header, 20);
        BitConverter.GetBytes((UInt32)24).CopyTo(header, 24);
        using (FileStream disk = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) {
            byte[] buffer = new byte[1024 * 1024];
            int got = disk.Read(buffer, 0, 512);
            bool owned = got == 512;
            for (int i = 0; i < 512 && owned; ++i) owned = buffer[i] == header[i];
            if (owned) return;
            disk.Position = 0;
            while ((got = disk.Read(buffer, 0, buffer.Length)) != 0)
                for (int i = 0; i < got; ++i)
                    if (buffer[i] != 0)
                        throw new IOException("HDD contains unknown data; it was left unchanged: " + path);
            disk.Position = 0;
            disk.Write(header, 0, header.Length);
            disk.Flush();
        }
    }
}
'@

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
    [TownscraftTerrainDisk]::Initialize($hardDisk)
    if ($PrepareDiskOnly) {
        [pscustomobject]@{ HardDisk = $hardDisk; Bytes = $diskBytes; ScratchVersion = 1 } | ConvertTo-Json
        exit 0
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
