# SM Mod Loader: install helpers shared by every mod package (the same file in each of them).
#
# The loader takes the place of Release\vcruntime140_1.dll (the original is kept as
# vcruntime140_1_orig.dll and keeps doing its job) and loads every DLL from Release\DLLModules.
# SM-DLL-Injector loads mods from the same folder, so when it is installed the loader is not needed.

$script:LoaderProduct = 'SM Mod Loader'

function Get-DllProduct([string]$Path) {
    if (-not (Test-Path $Path)) { return $null }
    (Get-Item $Path).VersionInfo.ProductName
}

function Get-DllVersion([string]$Path) {
    $text = (Get-Item $Path).VersionInfo.ProductVersion
    if (-not $text) { return [version]'0.0' }
    [version]$text
}

function Test-CompanyDll([string]$Path, [string]$Company) {
    (Test-Path $Path) -and ((Get-Item $Path).VersionInfo.CompanyName -match $Company)
}

# Copies a file and strips the "downloaded from the internet" mark from the copy
function Copy-ModFile([string]$Source, [string]$Target) {
    Copy-Item $Source $Target -Force
    Unblock-File $Target -ErrorAction SilentlyContinue
}

# Does the file contain the text (ASCII, or UTF-16 with -Wide)? Compared byte for byte.
function Test-DllContains([string]$Path, [string]$Text, [switch]$Wide) {
    if (-not (Test-Path $Path)) { return $false }
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)  # one char per byte
    $needle = if ($Wide) { [System.Text.Encoding]::Unicode.GetBytes($Text) } else { [System.Text.Encoding]::ASCII.GetBytes($Text) }
    $latin1.GetString([System.IO.File]::ReadAllBytes($Path)).Contains($latin1.GetString($needle))
}

# Time Stop in Pause 1.0 standing in for vcruntime140_1.dll (early builds have no version info)
function Test-LegacyTimeStop([string]$Path) {
    if ((Get-DllProduct $Path) -eq 'Time Stop in Pause') { return $true }
    (Test-DllContains $Path 'vcruntime140_1_orig.__CxxFrameHandler4') -and (Test-DllContains $Path 'TimeStopInPause.log' -Wide)
}

# Chest Hotkeys 1.0 standing in for BugSplat64.dll
function Test-LegacyChestHotkeys([string]$Path) {
    if ((Get-DllProduct $Path) -eq 'Chest Hotkeys') { return $true }
    (Test-DllContains $Path 'BugSplat64_orig.') -and (Test-DllContains $Path 'ChestHotkeys.log' -Wide)
}

# SM-DLL-Injector keeps the original runtime as vcruntime140_1_.dll
function Test-SmDllInjector([string]$Release) {
    Test-Path (Join-Path $Release 'vcruntime140_1_.dll')
}

# Moves a file into DLLModules unless a copy is already there; returns nothing
function Move-IntoModules([string]$Source, [string]$Target) {
    if (-not (Test-Path $Source)) { return }
    if (Test-Path $Target) { Remove-Item $Source -Force }
    else { Move-Item $Source $Target }
}

# Earlier versions of Time Stop in Pause (vcruntime140_1.dll) and Chest Hotkeys (BugSplat64.dll)
# replaced a game DLL themselves. Moves them into DLLModules and restores the original game DLL.
function Move-LegacyMods([string]$Release) {
    $modules = Join-Path $Release 'DLLModules'

    $vc = Join-Path $Release 'vcruntime140_1.dll'
    $vcOrig = Join-Path $Release 'vcruntime140_1_orig.dll'
    if ((Test-LegacyTimeStop $vc) -and (Test-Path $vcOrig)) {
        Move-IntoModules $vc (Join-Path $modules 'TimeStopInPause.dll')
        Move-IntoModules (Join-Path $Release 'TimeStopInPause.ini') (Join-Path $modules 'TimeStopInPause.ini')
        Remove-Item (Join-Path $Release 'TimeStopInPause.log') -ErrorAction SilentlyContinue
        # vcruntime140_1.dll is now missing; Install-ModLoader puts the loader there
    }

    $bugsplat = Join-Path $Release 'BugSplat64.dll'
    $bugsplatOrig = Join-Path $Release 'BugSplat64_orig.dll'
    if ((Test-LegacyChestHotkeys $bugsplat) -and (Test-Path $bugsplatOrig)) {
        Move-IntoModules $bugsplat (Join-Path $modules 'ChestHotkeys.dll')
        Move-Item $bugsplatOrig $bugsplat
        Move-IntoModules (Join-Path $Release 'ChestHotkeys.ini') (Join-Path $modules 'ChestHotkeys.ini')
        Remove-Item (Join-Path $Release 'ChestHotkeys.log') -ErrorAction SilentlyContinue
    }
    elseif ((Test-Path $bugsplatOrig) -and (Test-CompanyDll $bugsplat 'BugSplat')) {
        # Steam has restored the original BugSplat64.dll - only the leftover copy remains
        Remove-Item $bugsplatOrig -Force
    }
}

# Makes sure the game loads the mods from Release\DLLModules.
# Returns the name of the loader that does it: 'SM Mod Loader' or 'SM-DLL-Injector'.
function Install-ModLoader([string]$Release, [string]$LoaderDll) {
    $modules = Join-Path $Release 'DLLModules'
    $vc = Join-Path $Release 'vcruntime140_1.dll'
    $vcOrig = Join-Path $Release 'vcruntime140_1_orig.dll'

    if (Test-SmDllInjector $Release) {
        New-Item -ItemType Directory -Force $modules | Out-Null
        Move-LegacyMods $Release
        return 'SM-DLL-Injector'
    }

    if ((Test-Path $vc) -and (Get-DllProduct $vc) -ne $script:LoaderProduct -and
        -not (Test-LegacyTimeStop $vc) -and -not (Test-CompanyDll $vc 'Microsoft')) {
        throw "$vc has been replaced by another mod loader. Installation stopped so that it is not broken. If that loader loads mods from Release\DLLModules, copy the mod there by hand (see README.txt, manual install)."
    }

    New-Item -ItemType Directory -Force $modules | Out-Null
    Move-LegacyMods $Release

    if (Test-CompanyDll $vc 'Microsoft') {
        # A leftover from an earlier install that Steam has undone ("Verify integrity of game files")
        if (Test-Path $vcOrig) { Remove-Item $vcOrig -Force }
        Move-Item $vc $vcOrig
        Copy-ModFile $LoaderDll $vc
    }
    elseif (-not (Test-Path $vc)) {
        if (-not (Test-Path $vcOrig)) {
            throw "$vc is missing. Verify the game files in Steam, then run Install.bat again."
        }
        Copy-ModFile $LoaderDll $vc
    }
    else {
        if (-not (Test-Path $vcOrig)) {
            throw "$vcOrig is missing. Verify the game files in Steam, then run Install.bat again."
        }
        if ((Get-DllVersion $LoaderDll) -gt (Get-DllVersion $vc)) {
            Copy-ModFile $LoaderDll $vc
        }
    }
    return $script:LoaderProduct
}

# Removes the loader once no mods are left in DLLModules and restores the original vcruntime140_1.dll
function Uninstall-ModLoaderIfUnused([string]$Release) {
    $modules = Join-Path $Release 'DLLModules'
    if ((Test-Path $modules) -and (Get-ChildItem $modules -Filter '*.dll' -File)) { return }
    if (Test-SmDllInjector $Release) { return }

    $vc = Join-Path $Release 'vcruntime140_1.dll'
    $vcOrig = Join-Path $Release 'vcruntime140_1_orig.dll'
    if ((Get-DllProduct $vc) -eq $script:LoaderProduct) {
        if (-not (Test-Path $vcOrig)) {
            throw "$vcOrig is missing. Verify the game files in Steam - that restores the original DLL."
        }
        Remove-Item $vc -Force
        Move-Item $vcOrig $vc
    }
    elseif ((Test-Path $vcOrig) -and (Test-CompanyDll $vc 'Microsoft')) {
        # Steam has already restored the original - only the leftover copy remains
        Remove-Item $vcOrig -Force
    }
    Remove-Item (Join-Path $Release 'SMModLoader.log') -ErrorAction SilentlyContinue
    if ((Test-Path $modules) -and -not (Get-ChildItem $modules -Force)) {
        Remove-Item $modules -Force
    }
}
