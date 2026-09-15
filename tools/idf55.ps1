param([Parameter(ValueFromRemainingArguments=$true)][string[]]$IdfArgs)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'display_board'
$profile = 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1'
if (Test-Path -LiteralPath $profile) {
    . $profile
} elseif (Test-Path -LiteralPath 'C:\esp\v5.5.5\esp-idf\export.ps1') {
    . 'C:\esp\v5.5.5\esp-idf\export.ps1'
} else {
    throw 'Activate ESP-IDF 5.5.x or update this script with its installation path.'
}
Set-Location -LiteralPath $project
if (-not $IdfArgs) { $IdfArgs = @('build') }
& python "$env:IDF_PATH\tools\idf.py" -B build @IdfArgs
exit $LASTEXITCODE
