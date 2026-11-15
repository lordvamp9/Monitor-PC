$ErrorActionPreference = 'Stop'
$assets = Get-Content "$PSScriptRoot/sensors/obj/project.assets.json" -Raw | ConvertFrom-Json
$destination = "$PSScriptRoot/dist/licenses/packages"
New-Item -ItemType Directory -Force $destination | Out-Null
foreach ($library in $assets.libraries.PSObject.Properties) {
    $relative = $library.Value.path
    if (!$relative) { continue }
    foreach ($folder in $assets.packageFolders.PSObject.Properties.Name) {
        $packagePath = Join-Path $folder $relative
        if (!(Test-Path $packagePath)) { continue }
        $name = $library.Name.Replace('/', '-')
        $packageDestination = Join-Path $destination $name
        New-Item -ItemType Directory -Force $packageDestination | Out-Null
        Get-ChildItem $packagePath -File -Recurse |
            Where-Object { $_.Name -match '(?i)license|copyright|third.?party|notice|\.nuspec$' } |
            ForEach-Object {
                $relativeName = $_.FullName.Substring($packagePath.Length).TrimStart('\', '/').Replace('\', '_').Replace('/', '_')
                Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $packageDestination $relativeName)
            }
    }
}
