$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\src\settings\JSONSettings.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw

if ($source -notmatch 'onlyVendors\s*=\s*true\s*;') {
    throw 'onlyVendors=true is not assigned when the JSON option is enabled.'
}

if ($source -match '(?m)^\s*onlyVendors\s*;\s*$') {
    throw 'A no-op onlyVendors expression is present; vendor-only rules can leak to normal containers.'
}

Write-Host 'onlyVendors parser regression check passed.'
