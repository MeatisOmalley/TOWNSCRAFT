$ErrorActionPreference = 'Stop'
$gameRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixtureRoot = Join-Path $gameRoot ('build\disk-safety-' + [Guid]::NewGuid().ToString('N'))
$fixtureRuntime = Join-Path $fixtureRoot 'runtime_1989'
New-Item -ItemType Directory -Path $fixtureRuntime -Force | Out-Null
$fixtureIso = Join-Path $fixtureRoot 'fixture.ISO'
Copy-Item -LiteralPath (Join-Path $gameRoot 'TOWNSCRAFT.ISO') -Destination $fixtureIso
$fixtureDisk = Join-Path $fixtureRuntime 'fixture.HDD0.h0'
$fixtureStream = [IO.File]::Open($fixtureDisk, [IO.FileMode]::CreateNew)
try {
    $fixtureStream.SetLength(200000000)
    $fixtureStream.Position = 100000000
    $fixtureStream.WriteByte(77) # Zero boot sector does NOT mean empty disk.
} finally { $fixtureStream.Dispose() }
$initialHash = (Get-FileHash -LiteralPath $fixtureDisk -Algorithm SHA256).Hash
$helper = Join-Path $gameRoot 'tools\run_1989.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $helper -IsoPath $fixtureIso -PrepareDiskOnly
if ($LASTEXITCODE -ne 1) { throw 'Unknown-data disk was not refused.' }
if ((Get-FileHash -LiteralPath $fixtureDisk -Algorithm SHA256).Hash -ne $initialHash) { throw 'Unknown-data disk was modified.' }
# Only this newly generated, explicit fixture byte is cleared, not user data.
$fixtureStream = [IO.File]::Open($fixtureDisk, [IO.FileMode]::Open)
try { $fixtureStream.Position = 100000000; $fixtureStream.WriteByte(0) } finally { $fixtureStream.Dispose() }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $helper -IsoPath $fixtureIso -PrepareDiskOnly
if ($LASTEXITCODE -ne 0) { throw 'Fully blank disk preparation failed.' }
$ownedHash = (Get-FileHash -LiteralPath $fixtureDisk -Algorithm SHA256).Hash
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $helper -IsoPath $fixtureIso -PrepareDiskOnly
if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $fixtureDisk -Algorithm SHA256).Hash -ne $ownedHash) { throw 'Re-preparing owned disk modified it.' }
Write-Output 'PASS: zero-header unknown-data disk refused unchanged; fully blank disk marked; repeat preparation unchanged.'
