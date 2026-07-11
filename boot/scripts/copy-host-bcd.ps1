param(
    [Parameter(Mandatory = $true)]
    [string]$Destination
)

$espType = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$partitions = Get-Partition -ErrorAction SilentlyContinue |
    Where-Object { $_.GptType -eq $espType -or $_.Type -eq 'System' } |
    Sort-Object -Property @{ Expression = 'IsSystem'; Descending = $true }

foreach ($partition in $partitions) {
    foreach ($accessPath in $partition.AccessPaths) {
        $candidate = Join-Path $accessPath 'EFI\Microsoft\Boot\BCD'
        if (Test-Path -LiteralPath $candidate) {
            try {
                Copy-Item -LiteralPath $candidate -Destination $Destination -Force -ErrorAction Stop
                Write-Host "Copied host Windows BCD from $candidate"
                exit 0
            } catch {
                Write-Host "Cannot copy host Windows BCD directly: $($_.Exception.Message)"
            }
        }
    }
}

try {
    & bcdedit.exe /export $Destination | Out-Null
    if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $Destination)) {
        Write-Host 'Exported host Windows BCD with bcdedit.'
        exit 0
    }
} catch {
    Write-Host "bcdedit export failed: $($_.Exception.Message)"
}

Write-Host 'Host Windows BCD not found or not readable.'
exit 1
