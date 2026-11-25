# Fails unless every DLL that every executable and DLL in an unpacked package imports is
# either in the package (beside nec-workbench.exe, or beside the file that imports it) or a
# Windows system DLL. The Visual C++ runtime never counts as a system DLL, even on a machine
# that has it installed, so a package that would need the redistributable fails here.
# Prints each file's imports and the package's DLLs.
# Usage: check-imports.ps1 -Package <folder> -Compiler <cl.exe CMake used>
param([Parameter(Mandatory)] [string] $Package, [Parameter(Mandatory)] [string] $Compiler)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot PeImports.psm1)

$dumpbin = Get-Dumpbin $Compiler
$root = (Resolve-Path $Package).Path
$missing = @()
$report = @()
foreach ($file in Get-ChildItem $root -Recurse -File -Include *.exe, *.dll | Sort-Object FullName) {
    $relative = $file.FullName.Substring($root.Length + 1)
    $shipped = @()
    foreach ($name in Get-PeImports $dumpbin $file.FullName) {
        if ((Test-Path (Join-Path $root $name)) -or (Test-Path (Join-Path $file.DirectoryName $name))) {
            $shipped += $name
        } elseif (-not (Test-WindowsSystemDll $name)) {
            $missing += "$relative imports $name"
        }
    }
    $report += "$relative -> $(if ($shipped) { $shipped -join ', ' } else { 'Windows only' })"
}
$dlls = Get-ChildItem $root -Recurse -File -Filter *.dll | ForEach-Object { $_.FullName.Substring($root.Length + 1) } |
    Sort-Object
$report += "DLLs in the package ($($dlls.Count)): $($dlls -join ', ')"
$report
if ($missing) {
    $missing | ForEach-Object { Write-Output "MISSING: $_" }
    Write-Output "$($missing.Count) imported DLLs are neither in the package nor Windows system DLLs"
    exit 1
}
exit 0
