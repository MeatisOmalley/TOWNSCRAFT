function Sync-SharedRelease {
    param(
        [string]$Vm,
        [object]$VmRecord,
        [string]$PublishedBoot,
        [object]$PublishedRecord,
        [string]$SharedIso
    )

    $boot = Join-Path $Vm 'boot.img'
    $manifest = Join-Path $Vm 'build.json'
    $expected = $PublishedRecord.boot_sha256
    $actual = if (Test-Path -LiteralPath $boot -PathType Leaf) {
        (Get-FileHash -LiteralPath $boot -Algorithm SHA256).Hash.ToLowerInvariant()
    } else { '' }
    if ($actual -eq $expected -and
        $VmRecord.boot_sha256 -eq $expected -and
        $VmRecord.iso_sha256 -eq $PublishedRecord.iso_sha256 -and
        $VmRecord.exe_sha256 -eq $PublishedRecord.exe_sha256) {
        return $false
    }

    $bootTemp = Join-Path $Vm ('.boot-' + [guid]::NewGuid().ToString('N') + '.img')
    $manifestTemp = Join-Path $Vm ('.build-' + [guid]::NewGuid().ToString('N') + '.json')
    $bootBackup = Join-Path $Vm ('.boot-backup-' + [guid]::NewGuid().ToString('N') + '.img')
    $manifestBackup = Join-Path $Vm ('.build-backup-' + [guid]::NewGuid().ToString('N') + '.json')
    try {
        [IO.File]::Copy($PublishedBoot, $bootTemp)
        $copied = (Get-FileHash -LiteralPath $bootTemp -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($copied -ne $expected) { throw 'Published boot image changed during synchronization.' }
        if (Test-Path -LiteralPath $boot -PathType Leaf) {
            [IO.File]::Replace($bootTemp, $boot, $bootBackup)
        } else {
            [IO.File]::Move($bootTemp, $boot)
        }
        $VmRecord.boot_sha256 = $expected
        $VmRecord.iso_sha256 = $PublishedRecord.iso_sha256
        $VmRecord.exe_sha256 = $PublishedRecord.exe_sha256
        if ($PublishedRecord.audio) {
            $VmRecord | Add-Member -MemberType NoteProperty -Name audio -Value $PublishedRecord.audio -Force
        }
        if ($PublishedRecord.sound_bank) {
            $VmRecord | Add-Member -MemberType NoteProperty -Name sound_bank -Value $PublishedRecord.sound_bank -Force
        }
        $VmRecord.shared_iso_path = $SharedIso
        $json = $VmRecord | ConvertTo-Json -Depth 10
        [IO.File]::WriteAllText($manifestTemp, $json + [Environment]::NewLine,
            (New-Object Text.UTF8Encoding($false)))
        [IO.File]::Replace($manifestTemp, $manifest, $manifestBackup)
        return $true
    } finally {
        if (Test-Path -LiteralPath $bootTemp) { Remove-Item -LiteralPath $bootTemp }
        if (Test-Path -LiteralPath $manifestTemp) { Remove-Item -LiteralPath $manifestTemp }
        if (Test-Path -LiteralPath $bootBackup) { Remove-Item -LiteralPath $bootBackup }
        if (Test-Path -LiteralPath $manifestBackup) { Remove-Item -LiteralPath $manifestBackup }
    }
}
