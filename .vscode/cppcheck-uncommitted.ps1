$trackedFiles = @(git diff --name-only --diff-filter=ACMRTUXB HEAD |
    Where-Object { $_ -match '\.(c|h)$' -and (Test-Path $_) })

$untrackedFiles = @(git ls-files --others --exclude-standard |
    Where-Object { $_ -match '\.(c|h)$' -and (Test-Path $_) })

$files = @($trackedFiles + $untrackedFiles | Sort-Object -Unique)

if (-not $files -or $files.Count -eq 0) {
    Write-Host 'No uncommitted C/C++ files to analyze.'
    exit 0
}

$reportDir = Join-Path $PSScriptRoot '..\.cppcheck'
$reportDir = [System.IO.Path]::GetFullPath($reportDir)
$reportFile = Join-Path $reportDir 'uncommitted-changes.cppcheck.xml'

New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

$listFile = Join-Path $env:TEMP ('cppcheck-uncommitted-' + [guid]::NewGuid().ToString() + '.txt')

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
