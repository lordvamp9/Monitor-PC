param()
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    & ./build.ps1 -WithSensors
    if ($LASTEXITCODE) { throw 'Build failed.' }
    New-Item -ItemType Directory -Force dist/tools,dist/licenses | Out-Null
    # Pin the collector version so its command-line/CSV contract is reproducible.
    $version = '2.3.0'
    $base = "https://github.com/GameTechDev/PresentMon"
    Invoke-WebRequest "$base/releases/download/v$version/PresentMon-$version-x64.exe" -OutFile dist/tools/PresentMon.exe
    if ((Get-FileHash dist/tools/PresentMon.exe -Algorithm SHA256).Hash -ne '690533CF5C2591EF571A3037AA4C205C8E75860EF3F56927BF3A98ACF7198485') {
        throw 'PresentMon checksum did not match the pinned release.'
    }
    foreach ($file in @('LICENSE.txt', 'THIRD_PARTY.txt')) {
        Invoke-WebRequest "https://raw.githubusercontent.com/GameTechDev/PresentMon/v$version/$file" -OutFile "dist/licenses/PresentMon-$file"
    }
    Copy-Item THIRD_PARTY_NOTICES.md dist/licenses/
    Invoke-WebRequest 'https://raw.githubusercontent.com/LibreHardwareMonitor/LibreHardwareMonitor/v0.9.4/LICENSE' -OutFile dist/licenses/LibreHardwareMonitor-MPL-2.0.txt
    Invoke-WebRequest 'https://raw.githubusercontent.com/LibreHardwareMonitor/LibreHardwareMonitor/v0.9.4/Licenses/License.html' -OutFile dist/licenses/LibreHardwareMonitor-ThirdParty.html
    # Include notices from every resolved NuGet dependency and the .NET runtime.
    & ./collect-notices.ps1
    Write-Host 'Collectors installed. Keep the complete dist directory together. See README for driver permissions and limitations.'
} finally { Pop-Location }
