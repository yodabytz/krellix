using System;
internal static class ProbeTest
{
    public static int Main()
    {
        foreach (byte value in new byte[] {0, 1, 50, 100})
            if (Probe.ReportedBattery(value) != value) throw new Exception("Valid Windows battery lost");
        foreach (object value in new object[] {null, (byte)101, (byte)255, -1, 50, "50", false, 12.5})
            if (Probe.ReportedBattery(value) != -1) throw new Exception("Invalid Windows property accepted");
        Console.WriteLine("PASS actual helper battery property conversion, zero, full, unknown and invalid types");
        return 0;
    }
}
