param([switch]$UseExistingCollectors)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (!$UseExistingCollectors) { & ./setup-collectors.ps1 }
    foreach ($file in @('dist/sensors/MonitorSensors.exe', 'dist/tools/PresentMon.exe', 'dist/licenses/THIRD_PARTY_NOTICES.md')) {
        if (!(Test-Path $file)) { throw "Missing package component: $file" }
    }
    $cabinet = Join-Path $PSScriptRoot 'dist/collectors.cab'
    $ddf = @(
        '.OPTION EXPLICIT',
        '.Set CabinetNameTemplate=collectors.cab',
        ".Set DiskDirectoryTemplate=`"$PSScriptRoot/dist`"",
        ".Set RptFileName=`"$PSScriptRoot/dist/collectors.rpt`"",
        ".Set InfFileName=`"$PSScriptRoot/dist/collectors.inf`"",
        '.Set Cabinet=on', '.Set Compress=on', '.Set CompressionType=LZX',
        '.Set MaxDiskSize=0', '.Set MaxCabinetSize=0', '.Set FolderSizeThreshold=0'
    )
    foreach ($directory in @('sensors', 'tools', 'licenses')) {
        Get-ChildItem "dist/$directory" -File -Recurse | Sort-Object FullName | ForEach-Object {
            $relative = $_.FullName.Substring((Join-Path $PSScriptRoot 'dist').Length + 1)
            # Cabinet tools flatten directory paths. Encode hierarchy in each
            # unique cabinet member name; the native extractor restores it.
            $member = $relative.Replace('\', '!').Replace('/', '!')
            $ddf += "`"$($_.FullName)`" `"$member`""
        }
    }
    $ddf | Set-Content dist/collectors.ddf -Encoding ascii
    & makecab.exe /F dist/collectors.ddf *> dist/cab-build.log
    if ($LASTEXITCODE) { Get-Content dist/cab-build.log | Select-Object -Last 15; throw 'Collector compression failed.' }
    if (!(Test-Path $cabinet)) { throw 'Collector cabinet was not created.' }
    $resource = (Get-Content resource.rc -Raw) + "`n201 RCDATA `"dist/collectors.cab`"`n"
    $resource | Set-Content dist/bundle.rc -Encoding ascii
    & windres -I . dist/bundle.rc -o dist/bundle.o
    if ($LASTEXITCODE) { throw 'Embedded resource compilation failed.' }
    & g++ -std=c++17 -Wall -Wextra -O2 -mwindows -static main.cpp hardware_monitor.cpp runtime_bundle.cpp dist/bundle.o -o dist/MonitorPC.exe -luser32 -lkernel32 -lgdi32 -lshell32 -ladvapi32 -lpdh -ldxgi -lsetupapi -lole32
    if ($LASTEXITCODE) { throw 'Single-executable compilation failed.' }
    # Exercise the same embedded resource without requesting elevation in CI.
    '201 RCDATA "dist/collectors.cab"' | Set-Content dist/bundle-test.rc -Encoding ascii
    & windres -I . dist/bundle-test.rc -o dist/bundle-test.o
    if ($LASTEXITCODE) { throw 'Bundle test resource compilation failed.' }
    & g++ -std=c++17 -Wall -Wextra -O2 -static tests/bundle_smoke.cpp hardware_monitor.cpp runtime_bundle.cpp dist/bundle-test.o -I. -o dist/bundle_smoke.exe -lpdh -ldxgi -ladvapi32 -luser32 -lsetupapi -lole32
    if ($LASTEXITCODE) { throw 'Bundle test compilation failed.' }
    & ./dist/bundle_smoke.exe
    if ($LASTEXITCODE) { throw 'Embedded collector integration test failed.' }
    Write-Host 'Ready: dist/MonitorPC.exe. All collectors, .NET runtime and license notices are embedded.'
} finally { Pop-Location }
