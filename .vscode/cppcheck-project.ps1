$includeDirs = @(
    'Boot\Inc',
    'Boot\Src',
    'Boot\Logger\Inc',
    'Boot\Logger\Src',
    'Core\Inc',
    'Core\Src',
    'FATFS\App',
    'FATFS\Target',
    'USB_HOST\App',
    'USB_HOST\Target'
)

$workspaceRoot = (Get-Location).Path
$reportDir = Join-Path $workspaceRoot '.cppcheck'
$reportFile = Join-Path $reportDir 'project-scan.cppcheck.xml'

$resolvedIncludeDirs = @(
    $includeDirs |
    Where-Object { Test-Path $_ } |
    ForEach-Object { (Resolve-Path $_).Path }
)

if (-not $resolvedIncludeDirs -or $resolvedIncludeDirs.Count -eq 0) {
    Write-Error 'No project source directories found for cppcheck.'
    exit 1
}

New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

$listFile = Join-Path $env:TEMP ('cppcheck-project-' + [guid]::NewGuid().ToString() + '.txt')

try {
    $files = @(rg --files $resolvedIncludeDirs -g '*.c' -g '*.h' |
        Where-Object { $_ } |
        Sort-Object -Unique)

    if (-not $files -or $files.Count -eq 0) {
        Write-Host 'No project C/C++ files found to analyze.'
        exit 0
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($listFile, $files, $utf8NoBom)

    $cppcheckArgs = @(
        '--xml',
        '--xml-version=2',
        '--enable=warning,style,performance,portability',
        '--std=c11',
        '--inline-suppr',
        '--quiet'
    )

    $cppcheckArgs += "--file-list=$listFile"
    $cppcheckArgs += "--output-file=$reportFile"

    & cppcheck @cppcheckArgs

    Write-Host "Cppcheck report written to: $reportFile"
    Write-Host "Scanned file count: $($files.Count)"
}
finally {
    if (Test-Path $listFile) {
        Remove-Item $listFile -Force
    }
}
