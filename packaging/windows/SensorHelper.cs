using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Management;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using LibreHardwareMonitor.Hardware;

internal static class SensorHelper
{
    private static readonly JavaScriptSerializer Json = new JavaScriptSerializer();
    private static string Short(string text) {
        var clean = new StringBuilder();
        foreach (char c in text) if (!char.IsControl(c)) clean.Append(c);
        text = clean.ToString().Trim();
        return text.Length > 128 ? text.Substring(0, 128) : text;
    }
    private static void Read(IHardware hardware, List<object> rows, int depth)
    {
        if (depth > 8 || rows.Count >= 512) return;
        // Emulated storage reports placeholders and fixed trip points, not host temperatures.
        string name = hardware.Name.ToLowerInvariant();
        if (hardware.HardwareType == HardwareType.Storage &&
            (name.Contains("vmware virtual") || name.Contains("vbox") || name.Contains("qemu") || name.Contains("virtual disk"))) return;
        hardware.Update();
        foreach (ISensor sensor in hardware.Sensors)
        {
            if (rows.Count >= 512) break;
            if (!sensor.Value.HasValue || float.IsNaN(sensor.Value.Value) || float.IsInfinity(sensor.Value.Value)) continue;
            if (sensor.SensorType != SensorType.Temperature && sensor.SensorType != SensorType.Fan && sensor.SensorType != SensorType.Voltage) continue;
            if (sensor.SensorType == SensorType.Temperature &&
                (sensor.Name == "Warning Temperature" || sensor.Name == "Critical Temperature")) continue;
            rows.Add(new { chip = Short(hardware.Name + " " + hardware.Identifier), label = Short(sensor.Name),
                type = sensor.SensorType.ToString(), value = sensor.Value.Value });
        }
        foreach (IHardware child in hardware.SubHardware) Read(child, rows, depth + 1);
    }
    private static bool IsVirtual()
    {
        try {
            using (var query = new ManagementObjectSearcher("SELECT Manufacturer,Model FROM Win32_ComputerSystem"))
            using (var results = query.Get())
                foreach (ManagementObject item in results) {
                    using (item) {
                        string model = (Convert.ToString(item["Manufacturer"]) + " " + Convert.ToString(item["Model"])).ToLowerInvariant();
                        if (model.Contains("vmware") || model.Contains("virtualbox") || model.Contains("virtual machine") || model.Contains("qemu")) return true;
                    }
                }
        } catch (ManagementException) { }
        return false;
    }
    private static int Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        bool once = args.Length == 1 && args[0] == "--once";
        Process parent = null;
        if (!once) {
            int pid;
            if (args.Length != 1 || !int.TryParse(args[0], out pid)) return 2;
            try { parent = Process.GetProcessById(pid); } catch (ArgumentException) { return 2; }
        }
        // Exit if Krellix crashes, including while a vendor sensor call is stuck.
        using (var watchdog = new Timer(delegate {
            if (parent != null) {
                try { if (parent.HasExited) Environment.Exit(0); }
                catch (InvalidOperationException) { Environment.Exit(0); }
            }
        }, null, 2000, 2000)) {
            var computer = new Computer { IsCpuEnabled = true, IsGpuEnabled = true,
                IsMotherboardEnabled = true, IsStorageEnabled = true, IsControllerEnabled = true };
            try {
                bool virtualMachine = IsVirtual();
                computer.Open();
                do {
                    var rows = new List<object>();
                    foreach (IHardware hardware in computer.Hardware) Read(hardware, rows, 0);
                    string status = rows.Count > 0 ? "ok" : virtualMachine ? "virtual-machine" : "unavailable";
                    Console.WriteLine(Json.Serialize(new { version = 1, status = status, readings = rows }));
                    Console.Out.Flush();
                    if (once) break;
                    Thread.Sleep(5000);
                } while (true);
                return 0;
            } catch (Exception error) {
                Console.Error.WriteLine(error.GetType().Name + ": " + error.Message);
                Console.WriteLine(Json.Serialize(new { version = 1, status = "error", readings = new object[0] }));
                return 1;
            } finally { computer.Close(); if (parent != null) parent.Dispose(); }
        }
    }
}
