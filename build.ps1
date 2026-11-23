[CmdletBinding()]
param(
    [ValidateRange(1, 64)]
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'

function Invoke-CheckedNative {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("Native command failed with exit code {0}: {1}" -f $LASTEXITCODE, $Command)
    }
}

$cmakeFound = Get-Command 'cmake' -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -ne $cmakeFound) {
    $cmakePath = $cmakeFound.Source
} else {
    $cmakePath = 'C:\Program Files\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
    throw 'CMake was not found. Install CMake and the Visual Studio 2026 C++ build tools before rebuilding.'
}

$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestPath -PathType Leaf)) {
    $ctestFound = Get-Command 'ctest' -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $ctestFound) {
        throw 'CTest was not found beside CMake or on PATH.'
    }
    $ctestPath = $ctestFound.Source
}
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'generated\game_data.h') -PathType Leaf)) {
    throw 'The embedded game-data header is missing. Generate it first with: python tools\embed_rom.py "path\to\your\Super Mario Bros.nes"'
}
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'dependencies\SDL3-3.4.18\cmake\SDL3Config.cmake') -PathType Leaf)) {
    throw 'The bundled SDL3 development dependency is missing from dependencies\SDL3-3.4.18.'
}

Invoke-CheckedNative -Command $cmakePath -Arguments @(
    '-S', $projectRoot,
    '-B', $buildRoot,
    '-G', 'Visual Studio 18 2026',
    '-A', 'x64'
)
Invoke-CheckedNative -Command $cmakePath -Arguments @(
    '--build', $buildRoot,
    '--config', 'Release',
    '--parallel', $Jobs.ToString()
)
Invoke-CheckedNative -Command $ctestPath -Arguments @(
    '--test-dir', $buildRoot,
    '--output-on-failure',
    '-C', 'Release'
)

foreach ($artifact in @('MarioPC.exe', 'SDL3.dll')) {
    $sourcePath = Join-Path (Join-Path $buildRoot 'Release') $artifact
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Build succeeded, but the required runtime file is missing: $sourcePath"
    }
    Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $projectRoot $artifact) -Force
}

Write-Host ''
Write-Host "Native Mario build and checks passed. Launch: $(Join-Path $projectRoot 'MarioPC.exe')"
Write-Host 'The game itself requires neither Python, the source ROM file, nor an Internet connection.'

