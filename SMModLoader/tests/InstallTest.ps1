# Installs the four mod packages into fake game folders one after another, in every order, and
# removes them again, checking that they share SM Mod Loader without conflicts: the loader goes in
# with the first mod and out with the last, is only ever replaced by a newer one, and each mod keeps
# its own files and settings. Nothing in the real game is touched.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tests\InstallTest.ps1 [-GameRelease <Release folder>]
# The packages are the unpacked dist folders of the four mods: run their release.bat first.
# The real game folder is only read: an original BugSplat64.dll is copied from it.

param(
    [string]$GameRelease = (Join-Path ${env:ProgramFiles(x86)} 'Steam\steamapps\common\Scrap Mechanic\Release')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$projects = Split-Path $root -Parent
$script:failures = 0

function Check([bool]$Ok, [string]$What) {
    if ($Ok) { Write-Host "  [OK  ] $What" } else { Write-Host "  [FAIL] $What" -ForegroundColor Red; $script:failures++ }
}

function Hash([string]$Path) { if (Test-Path $Path) { (Get-FileHash $Path).Hash } }
function Company([string]$Path) { if (Test-Path $Path) { (Get-Item $Path).VersionInfo.CompanyName } }

# --- The packages ---------------------------------------------------------------
$mods = @(
    # Gui: a texture the mod puts into Data\Gui
    [pscustomobject]@{ Name = 'TimeStopInPause'; Short = 'T'; Gui = $null; Dir = (Join-Path $projects 'Scrap Mechanic Time Stop in Pause\dist\TimeStopInPause') }
    [pscustomobject]@{ Name = 'ChestHotkeys';    Short = 'C'; Gui = $null; Dir = (Join-Path $projects 'Scrap Mechanic Chest Hotkeys\dist\ChestHotkeys') }
    [pscustomobject]@{ Name = 'ComfortLogic';    Short = 'M'; Gui = 'comfortlogic_clear_index.png'; Dir = (Join-Path $projects 'Scrap Mechanic Movable Logic\dist\ComfortLogic') }
    [pscustomobject]@{ Name = 'FastPhysics';     Short = 'F'; Gui = $null; Dir = (Join-Path $projects 'Scrap Mechanic Fast Physics\dist\FastPhysics') }
)
foreach ($mod in $mods) {
    foreach ($file in @('install.ps1', 'uninstall.ps1', 'common.ps1', 'modloader.ps1', 'SMModLoader.dll', "$($mod.Name).dll", "$($mod.Name).ini", $mod.Gui) | Where-Object { $_ }) {
        if (-not (Test-Path (Join-Path $mod.Dir "files\$file"))) {
            throw "$(Join-Path $mod.Dir "files\$file") not found. Run release.bat of $($mod.Name) first."
        }
    }
}

Write-Host '== Every package carries the current loader'
$loaderHash = Hash (Join-Path $root 'bin\SMModLoader.dll')
$scriptHash = Hash (Join-Path $root 'installer\modloader.ps1')
foreach ($mod in $mods) {
    Check ((Hash (Join-Path $mod.Dir 'files\SMModLoader.dll')) -eq $loaderHash -and
           (Hash (Join-Path $mod.Dir 'files\modloader.ps1')) -eq $scriptHash) "$($mod.Name): SMModLoader.dll and modloader.ps1 (otherwise run its release.bat again)"
}

# --- Fake games -----------------------------------------------------------------
$work = Join-Path ([IO.Path]::GetTempPath()) 'SMModInstallTest'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null

$microsoft = Join-Path $env:SystemRoot 'System32\vcruntime140_1.dll'
if (-not (Test-Path $microsoft)) { $microsoft = Join-Path $GameRelease 'vcruntime140_1_orig.dll' }
$microsoftHash = Hash $microsoft
$bugsplat = Get-ChildItem $GameRelease -Filter 'BugSplat64*.dll' | Where-Object { $_.VersionInfo.CompanyName -eq 'BugSplat' } | Select-Object -First 1
if (-not $bugsplat) { throw "No original BugSplat64.dll in $GameRelease" }

$script:games = 0
function New-Game {
    $script:games++
    $release = Join-Path $work "game$($script:games)\Scrap Mechanic\Release"
    New-Item -ItemType Directory $release -Force | Out-Null
    New-Item -ItemType File (Join-Path $release 'ScrapMechanic.exe') | Out-Null
    New-Item -ItemType Directory (Join-Path (Split-Path $release -Parent) 'Data\Gui') | Out-Null
    Copy-Item $microsoft (Join-Path $release 'vcruntime140_1.dll')
    Copy-Item $bugsplat.FullName (Join-Path $release 'BugSplat64.dll')
    return $release
}

# Files that look like the old standalone installs (they are recognized by their contents)
function New-Legacy([string]$Path, [string]$Ascii, [string]$Wide) {
    $bytes = [Text.Encoding]::ASCII.GetBytes("MZ fake $Ascii ") + [Text.Encoding]::Unicode.GetBytes($Wide) + [byte[]](0, 0)
    [IO.File]::WriteAllBytes($Path, $bytes)
}

# A game with Time Stop in Pause 1.0 (in place of vcruntime140_1.dll) and Chest Hotkeys 1.0
# (in place of BugSplat64.dll), each with its settings next to it
function New-LegacyGame {
    $release = New-Game
    $vc = Join-Path $release 'vcruntime140_1.dll'
    Move-Item $vc (Join-Path $release 'vcruntime140_1_orig.dll')
    New-Legacy $vc 'vcruntime140_1_orig.__CxxFrameHandler4' 'TimeStopInPause.log'
    Set-Content (Join-Path $release 'TimeStopInPause.ini') 'PauseMusic=0'
    Move-Item (Join-Path $release 'BugSplat64.dll') (Join-Path $release 'BugSplat64_orig.dll')
    New-Legacy (Join-Path $release 'BugSplat64.dll') 'BugSplat64_orig.?CreateMiniDump' 'ChestHotkeys.log'
    Set-Content (Join-Path $release 'ChestHotkeys.ini') 'Sort=G'
    return $release
}

function Run([string]$Script, [string]$Release) {
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $Script (Split-Path $Release -Parent) 2>&1
    $script:lastOutput = ($out | Out-String).Trim()
    return $LASTEXITCODE
}
function Install-Mod([object]$Mod, [string]$Release) { Run (Join-Path $Mod.Dir 'files\install.ps1') $Release }
function Uninstall-Mod([object]$Mod, [string]$Release) { Run (Join-Path $Mod.Dir 'files\uninstall.ps1') $Release }

# What is wrong with the game folder, given the mods that should be installed: DLLModules holds
# exactly these mods (the package DLLs) with their settings, plus $Extra; the loader is in place
# exactly while DLLModules has DLLs; the original game files are intact otherwise.
function Get-Problems([string]$Release, [object[]]$Installed, [string[]]$Extra = @(), [string]$Loader = $loaderHash) {
    $problems = @()
    $vc = Join-Path $Release 'vcruntime140_1.dll'
    $vcOrig = Join-Path $Release 'vcruntime140_1_orig.dll'
    $modules = Join-Path $Release 'DLLModules'

    $expected = @($Extra | Where-Object { $_ }) + @($Installed | ForEach-Object { "$($_.Name).dll"; "$($_.Name).ini" })
    $actual = @(if (Test-Path $modules) { Get-ChildItem $modules -File | ForEach-Object { $_.Name } })
    if ((($actual | Sort-Object) -join ', ') -ne (($expected | Sort-Object) -join ', ')) {
        $problems += "DLLModules has [$(($actual | Sort-Object) -join ', ')]"
    }
    foreach ($mod in $Installed) {
        if ((Hash (Join-Path $modules "$($mod.Name).dll")) -ne (Hash (Join-Path $mod.Dir "files\$($mod.Name).dll"))) {
            $problems += "$($mod.Name).dll is not the one from the package"
        }
    }
    if ($expected | Where-Object { $_ -like '*.dll' }) {
        if ((Hash $vc) -ne $Loader) { $problems += 'vcruntime140_1.dll is not the expected loader' }
        if ((Hash $vcOrig) -ne $microsoftHash) { $problems += 'vcruntime140_1_orig.dll is not the original' }
    }
    else {
        if ((Hash $vc) -ne $microsoftHash) { $problems += 'vcruntime140_1.dll is not the original' }
        if (Test-Path $vcOrig) { $problems += 'vcruntime140_1_orig.dll left behind' }
        if (Test-Path $modules) { $problems += 'DLLModules left behind' }
    }
    if ((Company (Join-Path $Release 'BugSplat64.dll')) -ne 'BugSplat' -or (Test-Path (Join-Path $Release 'BugSplat64_orig.dll'))) {
        $problems += 'BugSplat64.dll is not the original'
    }
    foreach ($mod in $mods) {
        foreach ($file in "$($mod.Name).ini", "$($mod.Name).log") {
            if (Test-Path (Join-Path $Release $file)) { $problems += "$file left in Release" }
        }
        if ($mod.Gui) {
            $texture = [IO.Path]::Combine((Split-Path $Release -Parent), 'Data\Gui', $mod.Gui)
            if ($Installed | Where-Object { $_.Name -eq $mod.Name }) {
                if ((Hash $texture) -ne (Hash (Join-Path $mod.Dir "files\$($mod.Gui)"))) { $problems += "Data\Gui\$($mod.Gui) is not the one from the package" }
            }
            elseif (Test-Path $texture) { $problems += "Data\Gui\$($mod.Gui) left behind" }
        }
    }
    return $problems
}

function Assert-State([string]$What, [string]$Release, [object[]]$Installed, [string[]]$Extra = @(), [string]$Loader = $loaderHash) {
    $problems = @(Get-Problems $Release $Installed $Extra $Loader)
    Check ($problems.Count -eq 0) ($What + $(if ($problems.Count) { ': ' + ($problems -join '; ') }))
}

function Names([object[]]$List) { ($List | ForEach-Object { $_.Short }) -join '' }

# --- Install in every order, remove in every order ---------------------------------
Write-Host '== Every install order, then removal (T = Time Stop in Pause, C = Chest Hotkeys, M = ComfortLogic, F = FastPhysics)'
function Get-Permutations([int[]]$Items) {
    if ($Items.Count -le 1) { return ,@($Items) }
    $result = @()
    foreach ($first in $Items) {
        foreach ($rest in (Get-Permutations @($Items | Where-Object { $_ -ne $first }))) { $result += ,(@($first) + @($rest)) }
    }
    return $result
}
$orders = @(Get-Permutations @(0..($mods.Count - 1)))
for ($i = 0; $i -lt $orders.Count; $i++) {
    $r = New-Game
    $installOrder = @($orders[$i] | ForEach-Object { $mods[$_] })
    $removeOrder = @($orders[$orders.Count - 1 - $i] | ForEach-Object { $mods[$_] })
    $problems = @()
    $installed = @()
    foreach ($mod in $installOrder) {
        if ((Install-Mod $mod $r) -ne 0) { $problems += "installing $($mod.Name) failed: $lastOutput" }
        $installed += $mod
        $problems += @(Get-Problems $r $installed | ForEach-Object { "after installing $($mod.Name): $_" })
    }
    foreach ($mod in $removeOrder) {
        if ((Uninstall-Mod $mod $r) -ne 0) { $problems += "removing $($mod.Name) failed: $lastOutput" }
        $installed = @($installed | Where-Object { $_.Name -ne $mod.Name })
        $problems += @(Get-Problems $r $installed | ForEach-Object { "after removing $($mod.Name): $_" })
    }
    Check ($problems.Count -eq 0) ("install $(Names $installOrder), remove $(Names $removeOrder)" + $(if ($problems.Count) { ': ' + ($problems -join '; ') }))
}

# --- Installing again ---------------------------------------------------------------
Write-Host '== Installing a mod again'
$r = New-Game
$modules = Join-Path $r 'DLLModules'
$ok = $true
foreach ($mod in $mods) { $ok = $ok -and ((Install-Mod $mod $r) -eq 0) }
$mySettings = @{
    TimeStopInPause = 'PauseMusic=0'
    ChestHotkeys    = 'Sort=G'
    ComfortLogic    = 'PickUpTime=0.8'
    FastPhysics     = 'Enabled=0'
}
foreach ($mod in $mods) { Set-Content (Join-Path $modules "$($mod.Name).ini") $mySettings[$mod.Name] }
$vc = Get-Item (Join-Path $r 'vcruntime140_1.dll')
$vc.LastWriteTime = [datetime]'2001-01-01'   # a copy over it would bring the package's date back
foreach ($mod in $mods) { $ok = $ok -and ((Install-Mod $mod $r) -eq 0) }
Check $ok 'each Install.bat runs twice'
Assert-State 'all four installed' $r $mods
Check (@($mods | Where-Object { (Get-Content (Join-Path $modules "$($_.Name).ini")) -ne $mySettings[$_.Name] }).Count -eq 0) 'the settings of all four kept'
Check ((Get-Item $vc.FullName).LastWriteTime -eq [datetime]'2001-01-01') 'the loader was not rewritten'
# ComfortLogic test builds had other settings (ShowMessages, Language): such a file is replaced
Set-Content (Join-Path $modules 'ComfortLogic.ini') "[ComfortLogic]`r`nShowMessages=1`r`nLanguage=Auto"
Check ((Install-Mod $mods[2] $r) -eq 0 -and
       (Hash (Join-Path $modules 'ComfortLogic.ini')) -eq (Hash (Join-Path $mods[2].Dir 'files\ComfortLogic.ini'))) 'an outdated ComfortLogic.ini is replaced'
# FastPhysics test builds (PhysLCP) had no Enabled= line: such a file is replaced too
Set-Content (Join-Path $modules 'FastPhysics.ini') "[FastPhysics]`r`nVerify=40"
Check ((Install-Mod $mods[3] $r) -eq 0 -and
       (Hash (Join-Path $modules 'FastPhysics.ini')) -eq (Hash (Join-Path $mods[3].Dir 'files\FastPhysics.ini'))) 'an outdated FastPhysics.ini is replaced'

# --- Movable Logic -------------------------------------------------------------------
Write-Host '== Movable Logic (the earlier name of ComfortLogic) installed, then ComfortLogic'
$r = New-Game
$modules = Join-Path $r 'DLLModules'
Check ((Install-Mod $mods[0] $r) -eq 0) 'Time Stop in Pause installs'
[IO.File]::WriteAllBytes((Join-Path $modules 'MovableLogic.dll'), [Text.Encoding]::ASCII.GetBytes('MZ Movable Logic'))
Set-Content (Join-Path $modules 'MovableLogic.ini') "; Movable Logic settings`r`n`r`n[MovableLogic]`r`nPickUpTime=0.8"
Set-Content (Join-Path $modules 'MovableLogic.log') 'log'
Check ((Install-Mod $mods[2] $r) -eq 0) 'ComfortLogic installs'
Assert-State 'Movable Logic is gone, ComfortLogic in its place' $r @($mods[0], $mods[2])
$ini = @(Get-Content (Join-Path $modules 'ComfortLogic.ini'))
Check ($ini -contains '[ComfortLogic]' -and $ini -contains 'PickUpTime=0.8' -and @($ini -like 'PickUpTime=*').Count -eq 1) 'its pick-up time is carried over into ComfortLogic.ini'
Check ((Uninstall-Mod $mods[2] $r) -eq 0 -and (Uninstall-Mod $mods[0] $r) -eq 0) 'both removed'
Assert-State 'the original game files are back' $r @()

Write-Host '== Movable Logic alone, removed with the ComfortLogic Uninstall.bat'
$r = New-Game
$modules = Join-Path $r 'DLLModules'
$ok = (Install-Mod $mods[2] $r) -eq 0
Move-Item (Join-Path $modules 'ComfortLogic.dll') (Join-Path $modules 'MovableLogic.dll')
Move-Item (Join-Path $modules 'ComfortLogic.ini') (Join-Path $modules 'MovableLogic.ini')
Remove-Item ([IO.Path]::Combine((Split-Path $r -Parent), 'Data\Gui', $mods[2].Gui))
Check ($ok -and (Uninstall-Mod $mods[2] $r) -eq 0) 'Uninstall.bat runs'
Assert-State 'Movable Logic and the loader are gone' $r @()

# --- A package with a newer loader ------------------------------------------------------
Write-Host '== A package with a newer loader'
$loaderVersion = (Get-Item (Join-Path $root 'bin\SMModLoader.dll')).VersionInfo.ProductVersion
$newerVersion = '9' + $loaderVersion.Substring(1)
$newer = [pscustomobject]@{ Name = 'TimeStopInPause'; Short = 'T'; Dir = (Join-Path $work 'newer\TimeStopInPause') }
New-Item -ItemType Directory (Split-Path $newer.Dir -Parent) | Out-Null
Copy-Item $mods[0].Dir $newer.Dir -Recurse
# The same loader with its version strings raised (the installers compare ProductVersion)
$newerDll = Join-Path $newer.Dir 'files\SMModLoader.dll'
$latin1 = [Text.Encoding]::GetEncoding(28591)
$text = $latin1.GetString([IO.File]::ReadAllBytes($newerDll))
$text = $text.Replace($latin1.GetString([Text.Encoding]::Unicode.GetBytes($loaderVersion)),
                      $latin1.GetString([Text.Encoding]::Unicode.GetBytes($newerVersion)))
[IO.File]::WriteAllBytes($newerDll, $latin1.GetBytes($text))
$newerHash = Hash $newerDll
Check ((Get-Item $newerDll).VersionInfo.ProductVersion -eq $newerVersion) "a Time Stop in Pause package with loader $newerVersion (the others have $loaderVersion)"

$r = New-Game
$ok = (Install-Mod $newer $r) -eq 0 -and (Install-Mod $mods[1] $r) -eq 0 -and (Install-Mod $mods[2] $r) -eq 0 -and (Install-Mod $mods[3] $r) -eq 0
Check $ok 'newer package first, then the others: all install'
Assert-State 'the newer loader stays' $r $mods -Loader $newerHash

$r = New-Game
$ok = (Install-Mod $mods[1] $r) -eq 0 -and (Install-Mod $newer $r) -eq 0 -and (Install-Mod $mods[2] $r) -eq 0 -and (Install-Mod $mods[3] $r) -eq 0
Check $ok 'Chest Hotkeys, then the newer package, then ComfortLogic and FastPhysics: all install'
Assert-State 'the loader is updated once and stays' $r $mods -Loader $newerHash
$ok = (Uninstall-Mod $mods[2] $r) -eq 0 -and (Uninstall-Mod $mods[0] $r) -eq 0 -and (Uninstall-Mod $mods[1] $r) -eq 0 -and (Uninstall-Mod $mods[3] $r) -eq 0
Check $ok 'all four removed'
Assert-State 'the original vcruntime140_1.dll is back' $r @()

# --- Old versions -----------------------------------------------------------------
Write-Host '== Time Stop in Pause 1.0 and Chest Hotkeys 1.0 installed the old way, then the new packages'
$r = New-LegacyGame
$modules = Join-Path $r 'DLLModules'
$legacyTimeStop = Hash (Join-Path $r 'vcruntime140_1.dll')
$legacyChest = Hash (Join-Path $r 'BugSplat64.dll')
Check ((Install-Mod $mods[2] $r) -eq 0) 'ComfortLogic installs first'
Check ((Hash (Join-Path $modules 'TimeStopInPause.dll')) -eq $legacyTimeStop -and (Hash (Join-Path $modules 'ChestHotkeys.dll')) -eq $legacyChest -and
       (Hash (Join-Path $r 'vcruntime140_1.dll')) -eq $loaderHash -and (Hash (Join-Path $r 'vcruntime140_1_orig.dll')) -eq $microsoftHash -and
       (Company (Join-Path $r 'BugSplat64.dll')) -eq 'BugSplat' -and -not (Test-Path (Join-Path $r 'BugSplat64_orig.dll'))) 'the old mods are moved into DLLModules, the original BugSplat64.dll is back'
Check ((Install-Mod $mods[0] $r) -eq 0 -and (Install-Mod $mods[1] $r) -eq 0 -and (Install-Mod $mods[3] $r) -eq 0) 'the new Time Stop in Pause, Chest Hotkeys and FastPhysics install'
Assert-State 'all four are the new versions' $r $mods
Check ((Get-Content (Join-Path $modules 'TimeStopInPause.ini')) -eq 'PauseMusic=0' -and (Get-Content (Join-Path $modules 'ChestHotkeys.ini')) -eq 'Sort=G') 'the old settings kept'

Write-Host '== Time Stop in Pause 1.0 and Chest Hotkeys 1.0 removed with the new Uninstall.bat'
$r = New-LegacyGame
$legacyTimeStop = Hash (Join-Path $r 'vcruntime140_1.dll')
Check ((Uninstall-Mod $mods[1] $r) -eq 0 -and (Company (Join-Path $r 'BugSplat64.dll')) -eq 'BugSplat' -and
       (Hash (Join-Path $r 'vcruntime140_1.dll')) -eq $legacyTimeStop) 'Chest Hotkeys removed, Time Stop in Pause 1.0 left alone'
Check ((Uninstall-Mod $mods[0] $r) -eq 0) 'Time Stop in Pause removed'
Assert-State 'the original game files are back' $r @()

# --- Steam restores vcruntime140_1.dll ------------------------------------------------------
Write-Host '== "Verify integrity of game files" in Steam between installs'
$r = New-Game
$ok = (Install-Mod $mods[0] $r) -eq 0 -and (Install-Mod $mods[1] $r) -eq 0
Copy-Item $microsoft (Join-Path $r 'vcruntime140_1.dll') -Force
Check ($ok -and (Install-Mod $mods[2] $r) -eq 0 -and (Install-Mod $mods[3] $r) -eq 0) 'ComfortLogic and FastPhysics install after the file check'
Assert-State 'the loader is back for all four' $r $mods
$ok = (Uninstall-Mod $mods[0] $r) -eq 0 -and (Uninstall-Mod $mods[1] $r) -eq 0 -and (Uninstall-Mod $mods[2] $r) -eq 0 -and (Uninstall-Mod $mods[3] $r) -eq 0
Check $ok 'all four removed'
Assert-State 'the original vcruntime140_1.dll is back' $r @()

# --- Another DLL mod -----------------------------------------------------------------
Write-Host '== Another DLL mod in DLLModules'
$r = New-Game
Check ((Install-Mod $mods[2] $r) -eq 0) 'ComfortLogic installs'
[IO.File]::WriteAllBytes((Join-Path $r 'DLLModules\OtherMod.dll'), [Text.Encoding]::ASCII.GetBytes('MZ other mod'))
Check ((Uninstall-Mod $mods[2] $r) -eq 0) 'ComfortLogic is removed'
Assert-State 'the loader stays for the other mod' $r @() -Extra 'OtherMod.dll'

# --- SM-DLL-Injector ----------------------------------------------------------------
Write-Host '== SM-DLL-Injector installed'
$r = New-Game
$vc = Join-Path $r 'vcruntime140_1.dll'
Move-Item $vc (Join-Path $r 'vcruntime140_1_.dll')
[IO.File]::WriteAllBytes($vc, [Text.Encoding]::ASCII.GetBytes('MZ injector'))
New-Item -ItemType Directory (Join-Path $r 'DLLModules') | Out-Null
$injectorHash = Hash $vc
$ok = $true
foreach ($mod in $mods) { $ok = $ok -and ((Install-Mod $mod $r) -eq 0) }
$dlls = @(Get-ChildItem (Join-Path $r 'DLLModules') -Filter '*.dll' | ForEach-Object { $_.Name } | Sort-Object) -join ', '
Check ($ok -and (Hash $vc) -eq $injectorHash -and -not (Test-Path (Join-Path $r 'vcruntime140_1_orig.dll')) -and
       $dlls -eq 'ChestHotkeys.dll, ComfortLogic.dll, FastPhysics.dll, TimeStopInPause.dll') 'all four go into DLLModules, SM-DLL-Injector left alone'
$ok = $true
foreach ($mod in $mods) { $ok = $ok -and ((Uninstall-Mod $mod $r) -eq 0) }
Check ($ok -and (Hash $vc) -eq $injectorHash -and (Test-Path (Join-Path $r 'vcruntime140_1_.dll')) -and
       -not (Get-ChildItem (Join-Path $r 'DLLModules'))) 'all four removed, SM-DLL-Injector left alone'

# --- Refusals ---------------------------------------------------------------------
Write-Host '== vcruntime140_1.dll replaced by an unknown program'
$r = New-Game
$vc = Join-Path $r 'vcruntime140_1.dll'
Move-Item $vc (Join-Path $r 'vcruntime140_1_orig.dll')
[IO.File]::WriteAllBytes($vc, [Text.Encoding]::ASCII.GetBytes('MZ some other proxy'))
$hash = Hash $vc
Check ((Install-Mod $mods[0] $r) -ne 0 -and (Hash $vc) -eq $hash -and -not (Test-Path (Join-Path $r 'DLLModules'))) 'installation stops and changes nothing'

Write-Host '== Wrong folder'
Check ((Install-Mod $mods[0] (Join-Path $work 'nowhere\Release')) -ne 0 -and $lastOutput -match 'not found') 'no game in the folder: clear error'

Remove-Item $work -Recurse -Force
if ($script:failures) { Write-Host "`n$($script:failures) check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host "`nAll checks passed"
