# Third-party software

Monitor PC's original C++ application and sensor bridge are MIT licensed. Dependencies retain their own licenses; the MIT license does not relicense them.

| Component | Version | License | Source |
| --- | --- | --- | --- |
| LibreHardwareMonitorLib | 0.9.4 | MPL-2.0 | https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/tree/v0.9.4 |
| PresentMon console collector | 2.3.0 | MIT and bundled third-party notices | https://github.com/GameTechDev/PresentMon/tree/v2.3.0 |
| .NET runtime and Microsoft support packages | Resolved at build time | MIT and component notices | https://github.com/dotnet/runtime |
| HidSharp | 2.1.0 | Apache-2.0 | https://www.zer7.com/software/hidsharp |
| Mono.Posix.NETStandard | 1.0.0 | MIT | https://github.com/mono/mono |

The sensor library is used without source modifications. Its corresponding source is available at the versioned link above. LibreHardwareMonitor includes additional components, including low-level sensor access libraries; its bundled third-party license document must accompany redistribution.

`setup-collectors.ps1` downloads the pinned PresentMon executable and license texts, copies LibreHardwareMonitor's MPL and third-party notices, and collects available license/notice files and package metadata from resolved NuGet dependencies into `dist/licenses/`. The self-contained .NET output also retains its runtime license and third-party notices. Keep these files with distributed builds. Review notices again when changing dependencies.

Windows and GPU drivers are supplied by their respective vendors and are not redistributed by this repository. Hardware and product names belong to their owners. Monitor PC is an independent project.
