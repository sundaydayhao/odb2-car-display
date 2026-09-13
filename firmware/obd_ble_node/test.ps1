param([string]$Cc = 'gcc')
$ErrorActionPreference='Stop'
$nodeRoot=$PSScriptRoot
$repoRoot=Split-Path (Split-Path $nodeRoot -Parent) -Parent
$out=Join-Path $nodeRoot 'build/host-tests'
New-Item -ItemType Directory -Force $out | Out-Null
$compiler=Get-Command $Cc -ErrorAction Stop
$env:PATH=(Split-Path $compiler.Source -Parent)+';'+$env:PATH
& $Cc -std=c11 -Wall -Wextra -Werror -I "$nodeRoot/main" -I "$repoRoot/main" `
    "$nodeRoot/tests/test_node.c" "$nodeRoot/main/node_core.c" "$nodeRoot/main/node_power.c" `
    "$repoRoot/main/elm327_protocol.c" -o "$out/test_node.exe"
if ($LASTEXITCODE) { throw 'Node host test compilation failed' }
& "$out/test_node.exe"
if ($LASTEXITCODE) { throw 'Node host tests failed' }
