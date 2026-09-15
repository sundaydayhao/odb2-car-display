param(
    [string]$IdfPath = $env:IDF_PATH,
    [string]$IdfProfile,
    [string]$Port,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$IdfArgs = @('build')
)
$ErrorActionPreference = 'Stop'
if ($IdfProfile) { . $IdfProfile }
elseif ($IdfPath -and (Test-Path (Join-Path $IdfPath 'export.ps1'))) { . (Join-Path $IdfPath 'export.ps1') }
elseif (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw 'Activate ESP-IDF 5.5, or pass -IdfPath / -IdfProfile.'
}
Push-Location $PSScriptRoot
try {
    if (-not $IdfArgs.Count) { $IdfArgs = @('build') }
    if ($Port) { $IdfArgs = @('-p', $Port) + $IdfArgs }
    & python (Join-Path $env:IDF_PATH 'tools/idf.py') @IdfArgs
    if ($LASTEXITCODE) { throw "idf.py failed: $LASTEXITCODE" }
} finally { Pop-Location }
