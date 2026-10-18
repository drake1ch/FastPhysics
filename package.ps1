# Packs the release archives into dist\:
#   FastPhysics-<version>.zip         - for players (main file on Nexus Mods)
#   FastPhysics-<version>-source.zip  - source code (optional file)
# Run build.bat first, or simply use release.bat.
# The mod loader (SMModLoader.dll and modloader.ps1) comes from the "Scrap Mechanic Mod Loader"
# project next to this one (SMModLoader\ in the source archive); release.bat builds it.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$root = $PSScriptRoot
$versionHeader = Get-Content (Join-Path $root 'src\version.h') -Raw
if ($versionHeader -notmatch 'FP_VERSION_STRING\s+"([^"]+)"') { throw 'FP_VERSION_STRING not found in src\version.h' }
$version = $Matches[1]

$dll = Join-Path $root 'bin\FastPhysics.dll'
if (-not (Test-Path $dll)) { throw 'bin\FastPhysics.dll not found. Run build.bat first.' }
$dllVersion = (Get-Item $dll).VersionInfo.ProductVersion
if ($dllVersion -ne $version) { throw "bin\FastPhysics.dll is version $dllVersion, src\version.h says $version. Run build.bat again." }

# Writes a zip with forward slashes in entry names (Compress-Archive in PowerShell 5.1 writes
# backslashes, which some tools show as flat file names). $entries: archive path -> file on disk.
function New-Zip([string]$ZipPath, [System.Collections.Specialized.OrderedDictionary]$Entries) {
    if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
    $zip = [System.IO.Compression.ZipFile]::Open($ZipPath, 'Create')
    try {
        foreach ($name in $Entries.Keys) {
            [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip, $Entries[$name], $name.Replace('\', '/'), 'Optimal')
        }
    }
    finally {
        $zip.Dispose()
    }
}

$loaderRoot = Join-Path $root 'SMModLoader'
if (-not (Test-Path (Join-Path $loaderRoot 'build.bat'))) {
    $loaderRoot = Join-Path (Split-Path $root -Parent) 'Scrap Mechanic Mod Loader'
}
$loaderDll = Join-Path $loaderRoot 'bin\SMModLoader.dll'
$loaderScript = Join-Path $loaderRoot 'installer\modloader.ps1'
if (-not (Test-Path $loaderDll)) { throw "$loaderDll not found. Build the mod loader first (release.bat does it)." }

$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null

# --- Player package: Install.bat, Uninstall.bat, README.txt on top, the rest in files\
$installer = Join-Path $root 'installer'
$main = [ordered]@{
    'FastPhysics\Install.bat'            = Join-Path $installer 'Install.bat'
    'FastPhysics\Uninstall.bat'          = Join-Path $installer 'Uninstall.bat'
    'FastPhysics\README.txt'             = Join-Path $installer 'README.txt'
    'FastPhysics\files\FastPhysics.dll'  = $dll
    'FastPhysics\files\FastPhysics.ini'  = Join-Path $installer 'files\FastPhysics.ini'
    'FastPhysics\files\SMModLoader.dll'  = $loaderDll
    'FastPhysics\files\install.ps1'      = Join-Path $installer 'files\install.ps1'
    'FastPhysics\files\uninstall.ps1'    = Join-Path $installer 'files\uninstall.ps1'
    'FastPhysics\files\common.ps1'       = Join-Path $installer 'files\common.ps1'
    'FastPhysics\files\modloader.ps1'    = $loaderScript
}
$mainZip = Join-Path $dist "FastPhysics-$version.zip"
New-Zip $mainZip $main

# The same layout unpacked, handy for testing Install.bat locally
$unpacked = Join-Path $dist 'FastPhysics'
if (Test-Path $unpacked) { Remove-Item $unpacked -Recurse -Force }
[System.IO.Compression.ZipFile]::ExtractToDirectory($mainZip, $dist)

# --- Source package
$sourceFiles = @(
    'README.md', 'build.bat', 'release.bat', 'package.ps1',
    'src\FastPhysics.cpp', 'src\FastPhysics.rc', 'src\version.h',
    'src\fastlcp.cpp', 'src\fastlcp.h', 'src\lcpkernels.h',
    'tests\SelfTest.cpp',
    'installer\Install.bat', 'installer\Uninstall.bat', 'installer\README.txt',
    'installer\files\FastPhysics.ini',
    'installer\files\install.ps1', 'installer\files\uninstall.ps1', 'installer\files\common.ps1'
)
$source = [ordered]@{}
foreach ($file in $sourceFiles) {
    $source["FastPhysics-$version-source\$file"] = Join-Path $root $file
}
# The mod loader shipped in the player package
foreach ($file in 'README.md', 'build.bat', 'src\SMModLoader.cpp', 'src\SMModLoader.def',
                  'src\SMModLoader.rc', 'src\version.h', 'installer\modloader.ps1',
                  'tests\LoaderTest.cpp', 'tests\TestMod.cpp', 'tests\InstallTest.ps1') {
    $source["FastPhysics-$version-source\SMModLoader\$file"] = Join-Path $loaderRoot $file
}
$sourceZip = Join-Path $dist "FastPhysics-$version-source.zip"
New-Zip $sourceZip $source

Write-Host "Packed FastPhysics ${version}:" -ForegroundColor Green
foreach ($zipPath in $mainZip, $sourceZip) {
    $size = [math]::Round((Get-Item $zipPath).Length / 1KB)
    Write-Host "  $zipPath ($size KB)"
}
