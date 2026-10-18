# Shared helpers for install.ps1 and uninstall.ps1
. (Join-Path $PSScriptRoot 'modloader.ps1')

function Find-GameRelease([string]$GamePath) {
    $candidates = @()
    if ($GamePath) {
        $candidates += $GamePath
        $candidates += [IO.Path]::Combine($GamePath, 'Release')
    }
    else {
        $libraries = @()
        $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
        if ($steam) {
            $libraries += $steam
            $vdf = [IO.Path]::Combine($steam, 'steamapps\libraryfolders.vdf')
            if (Test-Path $vdf) {
                foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                    $libraries += $m.Groups[1].Value.Replace('\\', '\')
                }
            }
        }
        $libraries += [IO.Path]::Combine(${env:ProgramFiles(x86)}, 'Steam')
        foreach ($lib in $libraries) {
            # Join-Path fails on drives that do not exist (e.g. a disconnected Steam library)
            $candidates += [IO.Path]::Combine($lib, 'steamapps\common\Scrap Mechanic\Release')
        }
    }
    foreach ($dir in $candidates) {
        if (Test-Path ([IO.Path]::Combine($dir, 'ScrapMechanic.exe'))) {
            return (Resolve-Path $dir).Path
        }
    }
    throw 'Scrap Mechanic was not found. Pass the game folder explicitly, for example: Install.bat "D:\SteamLibrary\steamapps\common\Scrap Mechanic"'
}

function Assert-GameNotRunning {
    if (Get-Process -Name 'ScrapMechanic' -ErrorAction SilentlyContinue) {
        throw 'Scrap Mechanic is running. Close the game and try again.'
    }
}

# A file of the package next to the installer scripts
function Find-PackageFile([string]$Name) {
    $path = Join-Path $PSScriptRoot $Name
    if (Test-Path $path) { return $path }
    throw "$Name was not found next to the installer. Extract the whole archive and run Install.bat again."
}

function Get-ModVersion([string]$Path) {
    (Get-Item $Path).VersionInfo.ProductVersion
}

# Puts FastPhysics.ini next to the mod without overwriting a configured one
function Copy-ModConfig([string]$TargetDir) {
    $source = Join-Path $PSScriptRoot 'FastPhysics.ini'
    $target = Join-Path $TargetDir 'FastPhysics.ini'
    if (-not (Test-Path $source)) { return }
    # A file from a test build without Enabled= only had settings that no longer exist
    if ((Test-Path $target) -and (Select-String -Path $target -Pattern '^\s*Enabled\s*=' -Quiet)) { return }
    Copy-ModFile $source $target
}
