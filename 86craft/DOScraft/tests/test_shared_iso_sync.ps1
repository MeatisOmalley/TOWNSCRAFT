$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$publishedVm = (Get-Content -LiteralPath (Join-Path $root 'build/game/486dx25/latest-play-vm.txt') -Raw).Trim()
$published = Get-Content -LiteralPath (Join-Path $publishedVm 'build.json') -Raw | ConvertFrom-Json
$fixture = Join-Path $root ('runtime/sync-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    $boot = Join-Path $fixture 'boot.img'
    $disk = Join-Path $fixture 'scratch.img'
    $cmos = Join-Path $fixture 'cmos.nvr'
    $manifest = Join-Path $fixture 'build.json'
    Set-Content -LiteralPath $boot -Value 'stale boot'
    Set-Content -LiteralPath $disk -Value 'saved world'
    Set-Content -LiteralPath $cmos -Value 'cmos state'
    $record = [pscustomobject]@{
        boot_sha256='old'; iso_sha256='old'; exe_sha256='old';
        shared_iso_path='old'; vm_directory=$fixture; target='dx2-80-1992'
    }
    $record | ConvertTo-Json | Set-Content -LiteralPath $manifest
    . (Join-Path $root 'ISO/Sync-Shared-Release.ps1')
    $args = @{ Vm=$fixture; VmRecord=$record; PublishedBoot=(Join-Path $publishedVm 'boot.img');
        PublishedRecord=$published; SharedIso=(Join-Path $root 'ISO/DOSCRAFT.ISO') }
    if (!(Sync-SharedRelease @args)) { throw 'Stale release was not refreshed.' }
    $updated = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    $args.VmRecord = $updated
    if (Sync-SharedRelease @args) { throw 'Current release was refreshed unnecessarily.' }
    if ($updated.iso_sha256 -ne $published.iso_sha256 -or
        $updated.exe_sha256 -ne $published.exe_sha256 -or
        $updated.boot_sha256 -ne $published.boot_sha256 -or
        (Get-FileHash -LiteralPath $boot -Algorithm SHA256).Hash.ToLowerInvariant() -ne $published.boot_sha256 -or
        (Get-Content -LiteralPath $disk -Raw).Trim() -ne 'saved world' -or
        (Get-Content -LiteralPath $cmos -Raw).Trim() -ne 'cmos state') {
        throw 'Shared release sync changed the wrong data.'
    }
    Write-Output 'Shared ISO stale-release refresh and repeat launch passed; disk and CMOS preserved.'
} finally {
    foreach ($name in @('boot.img','scratch.img','cmos.nvr','build.json')) {
        $path = Join-Path $fixture $name
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    Remove-Item -LiteralPath $fixture -Force
}
