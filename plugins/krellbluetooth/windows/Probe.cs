using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Devices.Enumeration;
using Windows.Devices.Radios;
using Windows.Foundation;
using Windows.Storage.Streams;

internal static class Probe
{
    internal static int ReportedBattery(object value)
    {
        // Documented property type is Byte; 101 means unknown, not a percentage.
        return value is byte && (byte)value <= 100 ? (byte)value : -1;
    }
    private static async Task<T> Bounded<T>(IAsyncOperation<T> operation)
    {
        var deadline = System.Diagnostics.Stopwatch.StartNew();
        while (operation.Status == AsyncStatus.Started && deadline.ElapsedMilliseconds < 1800)
            await Task.Delay(20);
        if (operation.Status == AsyncStatus.Started) {
            operation.Cancel();
            throw new TimeoutException("Bluetooth query timed out");
        }
        try { return operation.GetResults(); }
        finally { operation.Close(); }
    }

    private static object State(string state, string detail)
    {
        return new { state = state, detail = detail, devices = new object[0] };
    }

    private static async Task<int> Battery(string id)
    {
        // Never pair or maintain a new connection. Query only already-connected LE
        // devices and release our own references. Unsupported batteries stay unknown.
        using (BluetoothLEDevice device = await Bounded(BluetoothLEDevice.FromIdAsync(id))) {
            if (device == null || device.ConnectionStatus != BluetoothConnectionStatus.Connected) return -1;
            var services = await Bounded(device.GetGattServicesForUuidAsync(
                new Guid("0000180f-0000-1000-8000-00805f9b34fb"), BluetoothCacheMode.Cached));
            if (services.Status != GattCommunicationStatus.Success) return -1;
            int value = -1;
            try {
                // Multiple battery services may represent earbuds or a case. Do
                // not invent an average or assign one component's value to all.
                if (services.Services.Count != 1) return -1;
                var chars = await Bounded(services.Services[0].GetCharacteristicsForUuidAsync(
                    new Guid("00002a19-0000-1000-8000-00805f9b34fb"), BluetoothCacheMode.Cached));
                if (chars.Status != GattCommunicationStatus.Success || chars.Characteristics.Count != 1
                    || device.ConnectionStatus != BluetoothConnectionStatus.Connected) return -1;
                var result = await Bounded(chars.Characteristics[0].ReadValueAsync(BluetoothCacheMode.Uncached));
                if (result.Status == GattCommunicationStatus.Success && result.Value != null && result.Value.Length == 1) {
                    using (var reader = DataReader.FromBuffer(result.Value)) { value = reader.ReadByte(); }
                }
                return device.ConnectionStatus == BluetoothConnectionStatus.Connected && value <= 100 ? value : -1;
            } finally { foreach (var service in services.Services) service.Dispose(); }
        }
    }

    private static async Task<object> Snapshot()
    {
        var radios = await Bounded(Radio.GetRadiosAsync());
        var bluetooth = radios.Where(r => r.Kind == RadioKind.Bluetooth).ToArray();
        if (bluetooth.Length == 0) return State("no_adapter", "Windows exposes no Bluetooth radio.");
        if (!bluetooth.Any(r => r.State == RadioState.On))
            return State("radio_off", "Bluetooth radio is off, disabled, or unavailable. No settings were changed.");
        var rows = new List<object>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string[] properties = { "System.Devices.Aep.DeviceAddress", "System.Devices.Aep.IsConnected", "System.Devices.BatteryLife" };
        for (int pass = 0; pass < 2; ++pass) {
            bool le = pass == 0;
            string selector = le ? BluetoothLEDevice.GetDeviceSelectorFromConnectionStatus(BluetoothConnectionStatus.Connected)
                                 : BluetoothDevice.GetDeviceSelectorFromConnectionStatus(BluetoothConnectionStatus.Connected);
            var devices = await Bounded(DeviceInformation.FindAllAsync(selector, properties));
            if (devices.Count > 128) throw new InvalidOperationException("Bluetooth device limit exceeded");
            foreach (var info in devices) {
                object connected;
                if (info.Properties.TryGetValue("System.Devices.Aep.IsConnected", out connected)
                    && connected is bool && !(bool)connected) continue;
                object property;
                string address = info.Properties.TryGetValue("System.Devices.Aep.DeviceAddress", out property)
                    ? Convert.ToString(property) : "";
                string identity = String.IsNullOrWhiteSpace(address) ? info.Id : address;
                if (!seen.Add(identity)) continue;
                if (rows.Count >= 64) throw new InvalidOperationException("Connected device limit exceeded");
                object reported;
                int battery = info.Properties.TryGetValue("System.Devices.BatteryLife", out reported)
                    ? ReportedBattery(reported) : -1;
                string note = le ? "Battery not reported or standard Battery Service unavailable."
                                 : "Windows did not report a battery level for this classic Bluetooth device.";
                if (battery >= 0) note = "Windows-reported battery level; Windows controls its refresh age.";
                if (le && battery < 0) {
                    try {
                        battery = await Battery(info.Id);
                        if (battery >= 0) note = "Live standard BLE Battery Level";
                    }
                    catch (Exception) { note = "Battery query unavailable, denied, or timed out."; }
                }
                rows.Add(new { id = info.Id, name = info.Name, address = address, connected = true,
                    battery = battery, detail = note });
            }
        }
        return new { state = "ok", detail = "Connected Windows Bluetooth devices only.", devices = rows };
    }

    [STAThread]
    public static int Main()
    {
        object result = State("timeout", "Bluetooth query exceeded ten seconds; stale data cleared.");
        // FromIdAsync can require a UI-thread context for Windows consent. Use
        // a private STA message loop, without displaying a form or blocking Qt.
        using (var context = new System.Windows.Forms.ApplicationContext())
        using (var timer = new System.Windows.Forms.Timer()) {
            System.Threading.SynchronizationContext.SetSynchronizationContext(
                new System.Windows.Forms.WindowsFormsSynchronizationContext());
            bool completed = false;
            EventHandler start = null;
            start = async delegate {
                System.Windows.Forms.Application.Idle -= start;
                object snapshot;
                try { snapshot = await Snapshot(); }
                catch (Exception error) {
                    var cause = error.GetBaseException();
                    snapshot = State(cause is UnauthorizedAccessException ? "permission" : "error",
                        "Windows Bluetooth query unavailable (HRESULT 0x" + cause.HResult.ToString("X8") + ").");
                }
                if (!completed) { completed = true; result = snapshot; timer.Stop(); context.ExitThread(); }
            };
            timer.Interval = 10000;
            timer.Tick += delegate { completed = true; timer.Stop(); context.ExitThread(); };
            System.Windows.Forms.Application.Idle += start;
            timer.Start();
            System.Windows.Forms.Application.Run(context);
            System.Windows.Forms.Application.Idle -= start;
        }
        Console.OutputEncoding = new UTF8Encoding(false);
        Console.WriteLine(new JavaScriptSerializer().Serialize(result));
        return 0;
    }
}
