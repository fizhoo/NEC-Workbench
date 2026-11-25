# The DLL imports of Windows executables, read with dumpbin from the MSVC toolset that built
# NEC Workbench, for bundle-msvc-runtime.ps1 and check-imports.ps1.

# The Visual C++ runtime, which a clean Windows does not have and which must therefore be
# shipped app-locally, even though a build machine has it in System32.
$script:Redistributable = '^(vcruntime\d+(_\d+)?|msvcp\d+(_\w+)?|concrt\d+|vccorlib\d+|vcomp\d+|vcamp\d+|mfc\d+\w*|ucrtbased|libgcc\w*|libstdc\+\+\w*|libwinpthread\w*)\.dll$'

function Test-Redistributable([string] $Name) { return $Name -match $script:Redistributable }

# The toolset's own dumpbin, beside the cl.exe CMake used.
function Get-Dumpbin([string] $Compiler) {
    $dumpbin = Join-Path (Split-Path $Compiler) "dumpbin.exe"
    if (-not (Test-Path $dumpbin)) { throw "no dumpbin.exe beside $Compiler" }
    return $dumpbin
}

# The names of the DLLs a PE file imports, normally and delay-loaded.
function Get-PeImports([string] $Dumpbin, [string] $Path) {
    $output = & $Dumpbin /nologo /dependents $Path
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed on $Path" }
    return @($output | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $Matches[1] } |
        Sort-Object -Unique)
}

# A DLL that every supported Windows provides: an API set, or a file in System32 that is not
# part of a redistributable runtime.
function Test-WindowsSystemDll([string] $Name) {
    if ($Name -match '^(api|ext)-ms-') { return $true }
    if (Test-Redistributable $Name) { return $false }
    return Test-Path (Join-Path $env:SystemRoot "System32\$Name")
}

Export-ModuleMember -Function Test-Redistributable, Get-Dumpbin, Get-PeImports, Test-WindowsSystemDll
