param(
    [string]$Path = ".",
    [string]$Filter = "*.*"
)

$files = Get-ChildItem -Path $Path -Filter $Filter -Recurse | Where-Object {
    $_.Extension -match '\.(cpp|cppm|hpp|c|h|cs|py|js|ts|txt|json|xml|html|css)$'
}

foreach ($file in $files) {
    $content = Get-Content $file.FullName -Raw
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($content)
    
    if ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        Write-Host "已包含 BOM: $($file.FullName)" -ForegroundColor Gray
        continue
    }
    
    $utf8Bom = [System.Text.Encoding]::UTF8
    $utf8BomWithBom = New-Object System.Text.UTF8Encoding $true
    $contentWithoutBom = $utf8Bom.GetString($bytes)
    $contentWithBom = $utf8BomWithBom.GetString($utf8BomWithBom.GetBytes($contentWithoutBom))
    
    [System.IO.File]::WriteAllText($file.FullName, $contentWithoutBom, $utf8BomWithBom)
    Write-Host "converted $($file.FullName)" -ForegroundColor Green
}

Write-Host "Converstion done! File count $($files.Count)" -ForegroundColor Yellow