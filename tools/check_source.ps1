param(
    [string]$BuildDirectory = 'build/source-gate',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$PythonExecutable = (Get-Command python -ErrorAction Stop).Source
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$sourceBuild = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else { [IO.Path]::GetFullPath((Join-Path $sourceRoot $BuildDirectory)) }

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

Invoke-Checked 'cmake' @('-S', $sourceRoot, '-B', $sourceBuild,
    '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DMHE_SOURCE_GATE=ON',
    '-DMHE_MODULE_EXTENSION=', "-DPython3_EXECUTABLE=$PythonExecutable")
Invoke-Checked 'cmake' @('--build', $sourceBuild, '--config', $Configuration, '--parallel', '4')
Invoke-Checked 'ctest' @('--test-dir', $sourceBuild, '-C', $Configuration,
    '-L', '^source$', '--output-on-failure', '--no-tests=error')
