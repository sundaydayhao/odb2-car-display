param([string]$Compiler = 'C:\cygwin64\bin\gcc.exe', [string]$Python = 'C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe')
$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
$env:PATH = "$(Split-Path $Compiler -Parent);$env:PATH"
$Ninja = 'C:/Espressif/tools/ninja/1.12.1/ninja.exe'
Set-Location -LiteralPath $Root
foreach ($Pair in @(@('protocol_tests', 'host-protocol'), @('ui_preview', 'host-ui'))) {
    & cmake -S "tools/$($Pair[0])" -B "build/$($Pair[1])" -G Ninja "-DCMAKE_C_COMPILER=$Compiler" "-DCMAKE_MAKE_PROGRAM=$Ninja"
    if ($LASTEXITCODE -ne 0) { throw 'Host configure failed' }
    & cmake --build "build/$($Pair[1])" -j 8
    if ($LASTEXITCODE -ne 0) { throw 'Host build failed' }
}
& ctest --test-dir build/host-protocol --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Protocol/policy tests failed' }
New-Item -ItemType Directory -Path build/ui-p4 -Force | Out-Null
& "$Root/build/host-ui/dashboard_preview.exe" "$($Root.Replace('\','/'))/build/ui-p4"
if ($LASTEXITCODE -ne 0) { throw 'UI tests failed' }
& $Python -m unittest discover -s tools -p test_lab_server.py -v
if ($LASTEXITCODE -ne 0) { throw 'HTTPS/replay tests failed' }
