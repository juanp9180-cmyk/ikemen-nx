# Zip a folder with "/" separators (Compress-Archive in PowerShell 5.1 writes
# "\", which other unzip tools treat as part of the file name).
# Usage: zipdir.ps1 <base folder> <entry inside base to add> <out.zip>
param([string]$Base, [string]$Entry, [string]$Out)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$Base = (Resolve-Path $Base).Path.TrimEnd('\')
if (Test-Path $Out) { Remove-Item $Out }
$zip = [IO.Compression.ZipFile]::Open($Out, 'Create')
try {
  Get-ChildItem (Join-Path $Base $Entry) -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($Base.Length + 1).Replace('\', '/')
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $rel, 'Optimal') | Out-Null
  }
} finally { $zip.Dispose() }
