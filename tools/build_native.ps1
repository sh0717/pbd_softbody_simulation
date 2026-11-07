param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskCmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($taskCmakeCommand) {
    $taskCmake = $taskCmakeCommand.Source
} else {
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $taskVs = & $taskVswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $taskCmake = Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath $taskCmake)) { throw 'Install CMake and a C++20 compiler.' }
$taskCtest = Join-Path (Split-Path -Parent $taskCmake) 'ctest.exe'
& $taskCmake -S $taskRoot -B (Join-Path $taskRoot 'build/native')
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $taskCmake --build (Join-Path $taskRoot 'build/native') --config $Configuration --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $taskCtest --test-dir (Join-Path $taskRoot 'build/native') -C $Configuration --output-on-failure
exit $LASTEXITCODE
