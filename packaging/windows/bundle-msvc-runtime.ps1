# Copies the Visual C++ runtime DLLs that nec-workbench.exe and the deployed DLLs import,
# and those they import in turn, from the redistributable folder of the MSVC toolset that
# built Workbench (VC\Redist\MSVC\<version>\x64\Microsoft.VC14x.CRT) into the package,
# app-locally beside nec-workbench.exe, so it runs on a Windows without the Visual C++
# redistributable. Prints the redistributable version, for the license notice.
# Usage: bundle-msvc-runtime.ps1 -Package <folder> -Compiler <cl.exe CMake used>
param([Parameter(Mandatory)] [string] $Package, [Parameter(Mandatory)] [string] $Compiler)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot PeImports.psm1)

# ...\VC\Tools\MSVC\<tools version>\bin\Hostx64\x64\cl.exe
$tools = (Get-Item $Compiler).Directory.Parent.Parent.Parent
$vc = $tools.Parent.Parent.Parent
$series = ($tools.Name -split '\.')[0..1] -join '.'
$redist = Get-ChildItem (Join-Path $vc.FullName "Redist\MSVC") -Directory |
    Where-Object { $_.Name -like "$series.*" } | Sort-Object { [version] $_.Name } | Select-Object -Last 1
if (-not $redist) {
    throw "no redistributable for MSVC $($tools.Name) under $($vc.FullName)\Redist\MSVC: " +
        ((Get-ChildItem (Join-Path $vc.FullName "Redist\MSVC")).Name -join ', ')
}
$crt = @(Get-ChildItem (Join-Path $redist.FullName "x64") -Directory -Filter "Microsoft.VC*.CRT")
if ($crt.Count -ne 1) { throw "expected one Microsoft.VC*.CRT in $($redist.FullName)\x64" }
$crt = $crt[0]

$dumpbin = Get-Dumpbin $Compiler
$copied = @()
do {
    $added = $false
    $files = Get-ChildItem $Package -Recurse -File -Include *.exe, *.dll
    $wanted = $files | ForEach-Object { Get-PeImports $dumpbin $_.FullName } |
        Where-Object { Test-Redistributable $_ } | Sort-Object -Unique
    foreach ($name in $wanted) {
        if (Test-Path (Join-Path $Package $name)) { continue }
        $source = Join-Path $crt.FullName $name
        if (-not (Test-Path $source)) { throw "$name is imported but not in $($crt.FullName)" }
        Copy-Item $source $Package
        $copied += $name
        $added = $true
    }
} while ($added)
if ($copied.Count -eq 0) { throw "no Visual C++ runtime DLL is imported; expected vcruntime140.dll at least" }

$version = (Get-Item (Join-Path $Package "vcruntime140.dll")).VersionInfo.ProductVersion
Write-Output "Visual C++ runtime $version from $($crt.FullName) (toolset MSVC $($tools.Name)): $($copied -join ', ')"
