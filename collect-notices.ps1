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
# Runtime packs are SDK download dependencies, not entries in assets.libraries.
# Preserve their root license texts as well as ordinary NuGet package notices.
foreach ($folder in $assets.packageFolders.PSObject.Properties.Name) {
    Get-ChildItem $folder -Directory -Filter 'microsoft.netcore.app.runtime.*' |
        ForEach-Object {
            $runtimeName = $_.Name
            Get-ChildItem $_.FullName -Directory | ForEach-Object {
                $runtimeDestination = Join-Path $destination "$runtimeName-$($_.Name)"
                New-Item -ItemType Directory -Force $runtimeDestination | Out-Null
                Get-ChildItem $_.FullName -File |
                    Where-Object { $_.Name -match '(?i)license|third.?party|notice|\.nuspec$' } |
                    Copy-Item -Destination $runtimeDestination
            }
        }
}
