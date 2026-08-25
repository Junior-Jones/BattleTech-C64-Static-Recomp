param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\Build'),
    [string]$Gcc = 'C:\msys64\mingw64\bin\gcc.exe',
    [string]$Ar = 'C:\msys64\mingw64\bin\ar.exe',
    [string]$Windres = 'C:\msys64\mingw64\bin\windres.exe',
    [string]$Strip = 'C:\msys64\mingw64\bin\strip.exe'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = $PSScriptRoot
$workspaceRoot = (Resolve-Path -LiteralPath (Join-Path $projectRoot '..\..')).Path
$sdlRoot = Join-Path $projectRoot 'ThirdParty\SDL2\x86_64-w64-mingw32'
$sdlInclude = Join-Path $sdlRoot 'include\SDL2'
$sdlStatic = Join-Path $sdlRoot 'lib\libSDL2.a'
$sdlLicense = Join-Path $projectRoot 'ThirdParty\SDL2\LICENSE.txt'
$sdlVersionHeader = Join-Path $sdlInclude 'SDL_version.h'
foreach ($file in @($Gcc,$Ar,$Windres,$Strip,$sdlStatic,$sdlLicense,(Join-Path $sdlInclude 'SDL.h'),$sdlVersionHeader)) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required build file not found: $file" } }
$sdlStaticHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sdlStatic).Hash.ToLowerInvariant()
if ($sdlStaticHash -ne 'cf66b9a12cf191c17b6fdb61a457a8df854b0f6cb533588f6949655f7a37fd24') { throw "Unexpected SDL 2.32.10 static library hash: $sdlStaticHash" }
$sdlVersionHeaderHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sdlVersionHeader).Hash.ToLowerInvariant()
if ($sdlVersionHeaderHash -ne 'ee97570d0d6507b4687076336c07fd4a7fa1f5e7986c9b61893a279950febce8') { throw "Unexpected SDL 2.32.10 version header hash: $sdlVersionHeaderHash" }
$sdlSystemLibraries = @(
    '-Wl,--dynamicbase','-Wl,--nxcompat','-Wl,--high-entropy-va',
    '-lm','-ldinput8','-ldxguid','-ldxerr8','-luser32','-lgdi32','-lwinmm',
    '-limm32','-lole32','-loleaut32','-lshell32','-lsetupapi','-lversion','-luuid'
)

$sourceDirs = @(
    'Core\aot','Core\c64bus','Core\cia','Core\cpu6510','Core\diagnostic','Core\drive1541','Core\input','Core\kernal','Core\machine','Core\media','Core\public','Core\scheduler','Core\sid','Core\snapshot','Core\vicii',
    'Generated\C06\Source','Generated\NaturalRepair\Kernal','Generated\NaturalRepair\Fastloader','Generated\NaturalRepair\DriveUpload','Generated\NaturalRepair\Drive0400'
)
$includeDirs = @('Core','Core\aot','Core\c64bus','Core\cia','Core\cpu6510','Core\diagnostic','Core\drive1541','Core\input','Core\kernal','Core\machine','Core\media','Core\public','Core\scheduler','Core\sid','Core\snapshot','Core\vicii','Generated\C06\Source','Generated\NaturalRepair\Kernal','Generated\NaturalRepair\Fastloader','Generated\NaturalRepair\DriveUpload','Generated\NaturalRepair\Drive0400') | ForEach-Object { Join-Path $projectRoot $_ }

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$developmentArtifacts = @(
    (Join-Path $BuildDir 'obj'),
    (Join-Path $BuildDir 'libbt_static_core_runtime.a'),
    (Join-Path $BuildDir 'frontend-input-adapter-test.exe'),
    (Join-Path $BuildDir 'cpu6510-interrupt-test.exe'),
    (Join-Path $BuildDir 'windows-build-manifest.json'),
    (Join-Path $BuildDir 'battletech-c64.exe')
)
foreach ($artifact in $developmentArtifacts) {
    if (Test-Path -LiteralPath $artifact) { Remove-Item -LiteralPath $artifact -Recurse -Force }
}
$stagingDir = Join-Path $workspaceRoot ('.build-staging-' + [guid]::NewGuid().ToString('N'))
$objectDir = Join-Path $stagingDir 'obj'
New-Item -ItemType Directory -Force -Path $objectDir | Out-Null
try {
$commonFlags = @('-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-O2','-DNDEBUG','-ffunction-sections','-fdata-sections')
$includeFlags = foreach ($dir in $includeDirs) { "-I$dir" }
$sources = foreach ($dir in $sourceDirs) { Get-ChildItem -LiteralPath (Join-Path $projectRoot $dir) -File -Filter '*.c' }
$objects = [System.Collections.Generic.List[string]]::new()
Write-Host "Compiling $($sources.Count) static-core sources..."
foreach ($source in $sources) {
    $relative = [IO.Path]::GetRelativePath($projectRoot,$source.FullName)
    $objectName = ($relative -replace '[\\/:]','_') -replace '\.c$','.o'
    $objectPath = Join-Path $objectDir $objectName
    $compile = -not (Test-Path -LiteralPath $objectPath -PathType Leaf)
    if (-not $compile) { $compile = (Get-Item -LiteralPath $objectPath).LastWriteTimeUtc -lt $source.LastWriteTimeUtc }
    if ($compile) { & $Gcc @commonFlags @includeFlags -c $source.FullName -o $objectPath; if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $relative" } }
    $objects.Add($objectPath)
}

$library = Join-Path $stagingDir 'libbt_static_core_runtime.a'
if (Test-Path -LiteralPath $library) { Remove-Item -LiteralPath $library }
for ($index=0; $index -lt $objects.Count; $index+=16) { $last=[Math]::Min($index+15,$objects.Count-1); & $Ar rcs $library @($objects[$index..$last]); if ($LASTEXITCODE -ne 0) { throw 'Static library creation failed' } }

$resource = Join-Path $objectDir 'battletech_version.o'
& $Windres (Join-Path $projectRoot 'Frontend\Windows\battletech_version.rc') $resource
if ($LASTEXITCODE -ne 0) { throw 'Windows version resource compilation failed' }

$frontendIncludes = @("-I$(Join-Path $projectRoot 'Frontend\Windows')","-I$(Join-Path $projectRoot 'Frontend\common')","-I$(Join-Path $projectRoot 'Core\public')","-I$sdlInclude","-I$projectRoot")
$launcher = Join-Path $BuildDir 'Launcher.exe'
& $Gcc @commonFlags '-DSDL_STATIC' @frontendIncludes `
    (Join-Path $projectRoot 'Frontend\Windows\battletech_windows_entry.c') `
    (Join-Path $projectRoot 'Frontend\Windows\battletech_windows_launcher.c') `
    (Join-Path $projectRoot 'Frontend\Windows\battletech_frontend_sdl2.c') `
    (Join-Path $projectRoot 'Frontend\common\battletech_frontend_input.c') `
    $library $resource $sdlStatic @sdlSystemLibraries '-static-libgcc' '-municode' '-mwindows' '-Wl,--gc-sections' `
    '-lcomdlg32' '-ladvapi32' '-o' $launcher
if ($LASTEXITCODE -ne 0) { throw 'Unified Windows application link failed' }
& $Strip --strip-all $launcher
if ($LASTEXITCODE -ne 0) { throw 'Production executable stripping failed' }

$test = Join-Path $stagingDir 'frontend-input-adapter-test.exe'
& $Gcc @commonFlags @frontendIncludes (Join-Path $projectRoot 'Tests\Frontend\test_frontend_input_adapter.c') (Join-Path $projectRoot 'Frontend\common\battletech_frontend_input.c') '-static-libgcc' '-o' $test
if ($LASTEXITCODE -ne 0) { throw 'Frontend input test link failed' }

$cpuInterruptTest = Join-Path $stagingDir 'cpu6510-interrupt-test.exe'
& $Gcc @commonFlags @frontendIncludes (Join-Path $projectRoot 'Tests\Core\test_cpu6510_interrupt.c') $library '-static-libgcc' '-o' $cpuInterruptTest
if ($LASTEXITCODE -ne 0) { throw 'CPU6510 interrupt test link failed' }
& $test | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Frontend input tests failed' }
& $cpuInterruptTest | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'CPU6510 interrupt tests failed' }

$legacySdlDll = Join-Path $BuildDir 'SDL2.dll'
if (Test-Path -LiteralPath $legacySdlDll -PathType Leaf) { Remove-Item -LiteralPath $legacySdlDll }
$sdlLicenseOut = Join-Path $BuildDir 'SDL2-LICENSE.txt'
Copy-Item -LiteralPath $sdlLicense -Destination $sdlLicenseOut -Force
$systemOut = Join-Path $BuildDir 'System'; New-Item -ItemType Directory -Force -Path $systemOut | Out-Null
Get-ChildItem -LiteralPath (Join-Path $projectRoot 'System') -File | Copy-Item -Destination $systemOut -Force
$romOut = Join-Path $BuildDir 'Rom'; New-Item -ItemType Directory -Force -Path $romOut | Out-Null
$savesOut = Join-Path $BuildDir 'Saves'; New-Item -ItemType Directory -Force -Path $savesOut | Out-Null
$screenshotsOut = Join-Path $BuildDir 'Screenshots'; New-Item -ItemType Directory -Force -Path $screenshotsOut | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'Packaging\Rom-README.txt') -Destination (Join-Path $romOut 'README.txt') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'Packaging\Saves-README.txt') -Destination (Join-Path $savesOut 'README.txt') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'Packaging\Screenshots-README.txt') -Destination (Join-Path $screenshotsOut 'README.txt') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.txt') -Destination $BuildDir -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'VERSION.txt') -Destination $BuildDir -Force

Write-Host "Built official recomp version 1.0.0 in: $BuildDir"
}
finally {
    if (Test-Path -LiteralPath $stagingDir) { Remove-Item -LiteralPath $stagingDir -Recurse -Force }
}
