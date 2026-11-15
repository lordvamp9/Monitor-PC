param([switch]$WithSensors)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (!(Get-Command g++ -ErrorAction SilentlyContinue)) { throw 'Install MinGW-w64 and add g++ and windres to PATH.' }
    New-Item -ItemType Directory -Force dist | Out-Null
    & windres resource.rc -o dist/resource.o
    if ($LASTEXITCODE) { throw 'Resource compilation failed.' }
    & g++ -std=c++17 -Wall -Wextra -O2 -mwindows -static -static-libgcc -static-libstdc++ main.cpp hardware_monitor.cpp runtime_bundle.cpp dist/resource.o -o dist/hw_monitor.exe -luser32 -lkernel32 -lgdi32 -lshell32 -ladvapi32 -lpdh -ldxgi -lsetupapi -lole32
    if ($LASTEXITCODE) { throw 'C++ compilation failed.' }
    & g++ -std=c++17 -Wall -Wextra -O2 -static tests/telemetry_tests.cpp -I. -o dist/telemetry_tests.exe
    if ($LASTEXITCODE) { throw 'Test compilation failed.' }
    & ./dist/telemetry_tests.exe
    if ($LASTEXITCODE) { throw 'Telemetry tests failed.' }
    if ($WithSensors) {
        & dotnet publish sensors/MonitorSensors.csproj -c Release -r win-x64 --self-contained true -o dist/sensors
        if ($LASTEXITCODE) { throw 'Sensor helper build failed.' }
    }
    Copy-Item LICENSE dist/LICENSE
    Write-Host 'Built dist/hw_monitor.exe. Run setup-collectors.ps1 for real FPS and temperature support.'
} finally { Pop-Location }
