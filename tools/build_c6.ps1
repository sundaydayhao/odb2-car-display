param([string]$Profile = 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1')
$ErrorActionPreference = 'Stop'
$Repo = Split-Path $PSScriptRoot -Parent
$Display = Join-Path $Repo 'display_board'
$Hosted = Join-Path $Display 'managed_components\espressif__esp_hosted'
if (!(Test-Path -LiteralPath "$Hosted\slave\CMakeLists.txt")) { throw 'Build the P4 project first to resolve ESP-Hosted.' }
$Manifest = Get-Content -LiteralPath "$Hosted\idf_component.yml" -Raw
if ($Manifest -notmatch '(?m)^version:\s*2\.12\.11\s*$') { throw 'Expected ESP-Hosted 2.12.11; review both firmware versions before updating.' }
$Source = Join-Path $Display 'build\c6-source'
$Output = Join-Path $Display 'build\c6'
New-Item -ItemType Directory -Path $Source, $Output -Force | Out-Null
# Stage the official coprocessor project without modifying managed components.
foreach ($Part in @('slave', 'common')) {
    if (!(Test-Path -LiteralPath "$Source\$Part")) {
        Copy-Item -LiteralPath "$Hosted\$Part" -Destination $Source -Recurse
    }
}
. $Profile
$Python = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
& $Python "$env:IDF_PATH\tools\idf.py" -C "$Source\slave" -B $Output `
    '-DIDF_TARGET=esp32c6' "-DSDKCONFIG=$Output\sdkconfig" `
    "-DSDKCONFIG_DEFAULTS=$Source/slave/sdkconfig.defaults;$PSScriptRoot/c6.sdkconfig.defaults" build
if ($LASTEXITCODE -ne 0) { throw "C6 build failed ($LASTEXITCODE)" }
