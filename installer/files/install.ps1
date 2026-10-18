param(
    # Game folder ("Scrap Mechanic" or its "Release" subfolder). Found through Steam when omitted.
    [string]$GamePath
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $release = Find-GameRelease $GamePath
    Assert-GameNotRunning
    $modDll = Find-PackageFile 'FastPhysics.dll'
    $loaderDll = Find-PackageFile 'SMModLoader.dll'
    $version = Get-ModVersion $modDll

    $loader = Install-ModLoader $release $loaderDll
    $modules = Join-Path $release 'DLLModules'
    Copy-ModFile $modDll (Join-Path $modules 'FastPhysics.dll')
    Copy-ModConfig $modules
    Remove-Item (Join-Path $modules 'FastPhysics.log') -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $modules 'FastPhysics_fail.bin') -ErrorAction SilentlyContinue

    Write-Host "FastPhysics $version installed into $modules (loaded by $loader)." -ForegroundColor Green
    Write-Host ''
    Write-Host 'Nothing to set up: start the game. Big creations with many bearings, pistons and'
    Write-Host 'parts pressing against each other cost much less physics time now.'
    Write-Host "Settings: $(Join-Path $modules 'FastPhysics.ini')"
    Write-Host "Log:      $(Join-Path $modules 'FastPhysics.log')"
}
catch [System.UnauthorizedAccessException] {
    Write-Host 'No permission to write to the game folder. Run Install.bat as administrator.' -ForegroundColor Red
    exit 1
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
