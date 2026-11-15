using System.Diagnostics;
using System.Globalization;
using System.Text;
using LibreHardwareMonitor.Hardware;

// Machine-readable stdout only. Failure means unavailable; never estimate.
Console.OutputEncoding = new UTF8Encoding(false);
CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
using var parent = args.Length == 1 && int.TryParse(args[0], out var pid)
    ? Process.GetProcessById(pid) : null;
var computer = new Computer { IsCpuEnabled = true, IsGpuEnabled = true };
try
{
    computer.Open();
    while (parent is null || !parent.HasExited)
    {
        var cpus = new List<float>();
        var gpus = new List<(string Name, float Temp, float Load)>();
        foreach (var hardware in computer.Hardware)
        {
            try
            {
                Update(hardware);
                var temperatures = Sensors(hardware)
                    .Where(s => s.SensorType == SensorType.Temperature && s.Value.HasValue)
                    .Select(s => s.Value!.Value).Where(float.IsFinite).ToArray();
                if (hardware.HardwareType == HardwareType.Cpu) cpus.AddRange(temperatures);
                else if (hardware.HardwareType is HardwareType.GpuNvidia or HardwareType.GpuAmd or HardwareType.GpuIntel)
                {
                    // Select the core sensor; never substitute hotspot or memory temperature.
                    var core = Sensors(hardware).FirstOrDefault(s => s.SensorType == SensorType.Temperature &&
                        (s.Name.Equals("GPU Core", StringComparison.OrdinalIgnoreCase) ||
                         s.Name.Equals("GPU Temperature", StringComparison.OrdinalIgnoreCase)));
                    var load = Sensors(hardware).FirstOrDefault(s => s.SensorType == SensorType.Load &&
                        s.Name.Equals("GPU Core", StringComparison.OrdinalIgnoreCase));
                    gpus.Add((hardware.Name.Replace('\t', ' ').Replace('\n', ' '),
                        core?.Value is float t && float.IsFinite(t) ? t : -1,
                        load?.Value is float l && float.IsFinite(l) ? l : -1));
                }
            }
            catch (Exception ex) { Console.Error.WriteLine(ex.Message); }
        }
        // CPU reports the hottest available CPU sensor (package/core).
        Console.WriteLine($"C\t{(cpus.Count > 0 ? cpus.Max() : -1)}");
        foreach (var gpu in gpus) Console.WriteLine($"G\t{gpu.Name}\t{gpu.Temp}\t{gpu.Load}");
        Console.WriteLine("E");
        Console.Out.Flush();
        Thread.Sleep(1000);
    }
}
catch (Exception ex) { Console.Error.WriteLine(ex.Message); }
finally { computer.Close(); }

static void Update(IHardware hardware)
{
    hardware.Update();
    foreach (var child in hardware.SubHardware) Update(child);
}
static IEnumerable<ISensor> Sensors(IHardware hardware) => hardware.Sensors
    .Concat(hardware.SubHardware.SelectMany(Sensors));
