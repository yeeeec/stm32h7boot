$files = @(git diff --cached --name-only --diff-filter=ACMRTUXB |
    Where-Object { $_ -match '\.(c|h)$' -and (Test-Path $_) })

if (-not $files -or $files.Count -eq 0) {
    Write-Host 'No staged C/C++ files to analyze.'
    exit 0
}

$reportDir = Join-Path $PSScriptRoot '..\.cppcheck'
$reportDir = [System.IO.Path]::GetFullPath($reportDir)
$reportFile = Join-Path $reportDir 'staged-changes.cppcheck.xml'

New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

$listFile = Join-Path $env:TEMP ('cppcheck-staged-' + [guid]::NewGuid().ToString() + '.txt')

try {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($listFile, $files, $utf8NoBom)
    & cppcheck `
        --xml `
        --xml-version=2 `
        --enable=warning,style,performance,portability `
        --std=c11 `
        --inline-suppr `
        "--file-list=$listFile" `
        "--output-file=$reportFile"

    Write-Host "Cppcheck report written to: $reportFile"
}
finally {
    if (Test-Path $listFile) {
        Remove-Item $listFile -Force
    }
}
