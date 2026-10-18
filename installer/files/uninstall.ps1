param(
    # Game folder ("Scrap Mechanic" or its "Release" subfolder). Found through Steam when omitted.
    [string]$GamePath
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $release = Find-GameRelease $GamePath
    Assert-GameNotRunning

    $modules = Join-Path $release 'DLLModules'
    foreach ($file in 'FastPhysics.dll', 'FastPhysics.log', 'FastPhysics.ini', 'FastPhysics_fail.bin') {
        Remove-Item (Join-Path $modules $file) -Force -ErrorAction SilentlyContinue
    }

    # The loader stays while other mods use it
    Uninstall-ModLoaderIfUnused $release
    Write-Host 'FastPhysics has been removed.' -ForegroundColor Green
}
catch [System.UnauthorizedAccessException] {
    Write-Host 'No permission to write to the game folder. Run Uninstall.bat as administrator.' -ForegroundColor Red
    exit 1
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
