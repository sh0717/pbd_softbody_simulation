param(
    [ValidateSet('Setup', 'Configure', 'Build', 'Test', 'BuildWasm', 'TestWasm', 'CheckFormat', 'Format')]
    [string]$Action = 'Build',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$script:taskVisualStudio = $null

function Find-VisualStudio {
    if ($script:taskVisualStudio) { return $script:taskVisualStudio }
    $taskProgramFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $taskLocator = Join-Path $taskProgramFilesX86 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $taskLocator)) {
        throw 'Visual Studio Installer / MSVC is not installed.'
    }
    $script:taskVisualStudio = & $taskLocator -latest -products '*' -version '[18.0,19.0)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -or -not $script:taskVisualStudio) {
        throw 'The native-msvc preset requires Visual Studio 2026 with the C++ workload.'
    }
    return $script:taskVisualStudio
}

function Find-CMake {
    $taskCommand = Get-Command cmake -ErrorAction SilentlyContinue
    $taskCandidates = @()
    if ($taskCommand) { $taskCandidates += $taskCommand.Source }
    $taskCandidates += Join-Path (Find-VisualStudio) 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    foreach ($taskPath in ($taskCandidates | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $taskPath)) { continue }
        $taskVersionText = & $taskPath --version
        if ($LASTEXITCODE -ne 0) { continue }
        $taskVersionMatch = [regex]::Match(($taskVersionText -join ' '), 'cmake version (\d+\.\d+\.\d+)')
        if ($taskVersionMatch.Success -and [version]$taskVersionMatch.Groups[1].Value -ge [version]'4.2.0') {
            return $taskPath
        }
    }
    throw 'The Visual Studio 2026 preset requires CMake 4.2 or newer. Install/update the Visual Studio CMake tools.'
}

function Find-ClangFormat {
    $taskCommand = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($taskCommand) { return $taskCommand.Source }
    $taskPath = Join-Path (Find-VisualStudio) 'VC/Tools/Llvm/x64/bin/clang-format.exe'
    if (-not (Test-Path -LiteralPath $taskPath)) { throw 'Install the Visual Studio LLVM tools / clang-format.' }
    return $taskPath
}

function Invoke-Tool {
    param([string]$Executable, [string[]]$ToolArguments)
    & $Executable @ToolArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Executable $($ToolArguments -join ' ')"
    }
}

function Configure-Native {
    param([string]$CMake)
    Invoke-Tool $CMake @('--preset', 'native-msvc')
}

Push-Location -LiteralPath $taskRoot
try {
    switch ($Action) {
        'Setup' {
            $taskVisualStudio = Find-VisualStudio
            $taskToolsetFile = Join-Path $taskVisualStudio 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
            $taskToolset = (Get-Content -LiteralPath $taskToolsetFile -Raw).Trim()
            $taskCompiler = Join-Path $taskVisualStudio "VC/Tools/MSVC/$taskToolset/bin/Hostx64/x64/cl.exe"
            if (-not (Test-Path -LiteralPath $taskCompiler)) { throw 'The x64 MSVC compiler was not found.' }
            $taskCMake = Find-CMake
            $taskFormatter = Find-ClangFormat
            $taskSettingsPath = Join-Path $taskRoot '.vscode/settings.json'
            $taskSettings = Get-Content -LiteralPath $taskSettingsPath -Raw | ConvertFrom-Json -AsHashtable
            # 도구 경로는 이 프로젝트 설정에만 기록하며 사용자 전역 설정은 변경하지 않습니다.
            $taskSettings['cmake.cmakePath'] = $taskCMake.Replace('\', '/')
            $taskSettings['C_Cpp.default.compilerPath'] = $taskCompiler.Replace('\', '/')
            $taskSettings['C_Cpp.clang_format_path'] = $taskFormatter.Replace('\', '/')
            $taskSettings | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $taskSettingsPath -Encoding utf8
            $taskSdk = if ($env:EMSDK) { $env:EMSDK } else { Join-Path $taskRoot '.tools/emsdk-main' }
            $taskSdk = [IO.Path]::GetFullPath($taskSdk).Replace('\', '/')
            $taskCppSettingsPath = Join-Path $taskRoot '.vscode/c_cpp_properties.json'
            $taskCppSettings = @{
                version = 4
                configurations = @(
                    @{
                        name = 'Native MSVC (CMake)'
                        configurationProvider = 'ms-vscode.cmake-tools'
                        compilerPath = $taskCompiler.Replace('\', '/')
                        includePath = @('${workspaceFolder}/native/src', '${workspaceFolder}/native/bindings')
                        cppStandard = 'c++20'
                        intelliSenseMode = 'windows-msvc-x64'
                        browse = @{
                            path = @('${workspaceFolder}/native/src', '${workspaceFolder}/native/bindings', '${workspaceFolder}/tests/native')
                            limitSymbolsToIncludedHeaders = $true
                        }
                    },
                    @{
                        name = 'Wasm (Emscripten)'
                        compilerPath = "$taskSdk/upstream/bin/clang++.exe"
                        compilerArgs = @(
                            '--target=wasm32-unknown-emscripten'
                            "--sysroot=$taskSdk/upstream/emscripten/cache/sysroot"
                            '-isystem'
                            "$taskSdk/upstream/emscripten/cache/sysroot/include/compat"
                            '-fexceptions'
                        )
                        includePath = @('${workspaceFolder}/native/src', '${workspaceFolder}/native/bindings')
                        cppStandard = 'c++20'
                        intelliSenseMode = 'clang-x64'
                    }
                )
            }
            $taskCppSettings | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $taskCppSettingsPath -Encoding utf8
            $taskPrettier = Join-Path $taskRoot 'node_modules/prettier/bin/prettier.cjs'
            if (Test-Path -LiteralPath $taskPrettier) {
                Invoke-Tool 'node' @($taskPrettier, '--ignore-path', (Join-Path $taskRoot '.prettierignore'), '--write', $taskSettingsPath, $taskCppSettingsPath)
            }
            Write-Output "CMake: $taskCMake"
            Write-Output "MSVC: $taskCompiler"
            Write-Output "clang-format: $taskFormatter"
            Write-Output 'Workspace tools and Native/Wasm IntelliSense updated. Open pbd-lab.code-workspace in VS Code.'
        }
        'Configure' { Configure-Native (Find-CMake) }
        { $_ -in 'Build', 'Test' } {
            $taskCMake = Find-CMake
            $taskPreset = 'native-' + $Configuration.ToLowerInvariant()
            Configure-Native $taskCMake
            Invoke-Tool $taskCMake @('--build', '--preset', $taskPreset, '--parallel')
            if ($Action -eq 'Test') {
                $taskCTest = Join-Path (Split-Path -Parent $taskCMake) 'ctest.exe'
                if (-not (Test-Path -LiteralPath $taskCTest)) { throw "CTest was not found beside $taskCMake" }
                Invoke-Tool $taskCTest @('--preset', $taskPreset)
            }
        }
        { $_ -in 'BuildWasm', 'TestWasm' } {
            Invoke-Tool 'node' @('tools/run.mjs', 'build:all')
            if ($Action -eq 'TestWasm') {
                Invoke-Tool 'node' @('tools/test.mjs')
            }
        }
        { $_ -in 'CheckFormat', 'Format' } {
            $taskFiles = @(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'native'), (Join-Path $taskRoot 'tests/native') `
                -Recurse -File | Where-Object Extension -In '.cpp', '.h' | Sort-Object FullName | ForEach-Object FullName)
            if ($taskFiles.Count -eq 0) { throw 'No project C++ sources were found.' }
            [string[]]$taskFormatArguments = if ($Action -eq 'Format') { @('-i') } else { @('--dry-run', '--Werror') }
            Invoke-Tool (Find-ClangFormat) ($taskFormatArguments + $taskFiles)
            Write-Output "$Action completed for $($taskFiles.Count) C++ files."
        }
    }
} finally {
    Pop-Location
}
