using System;
using System.Collections.Generic;
using System.Text;
using System.Web.Script.Serialization;
using Microsoft.Management.Deployment;

internal static class Probe {
    static T Create<T>(string id) {
        return (T)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid(id)));
    }
    static string Bounded(string value) {
        if (value == null || value.Length > 2048) throw new InvalidOperationException("Invalid package metadata");
        return value;
    }
    [MTAThread]
    public static int Main() {
        Console.OutputEncoding = new UTF8Encoding(false);
        object result;
        try {
            var manager = Create<PackageManager>("C53A4F16-787E-42A4-B304-29EFFB4BF597");
            var source = manager.GetPackageCatalogByName("winget");
            if (source == null) throw new InvalidOperationException("WinGet community source is not configured");
            source.AcceptSourceAgreements = false;
            var options = Create<CreateCompositePackageCatalogOptions>("526534B8-7E46-47C8-8416-B1685C327D37");
            options.Catalogs.Add(source);
            options.CompositeSearchBehavior = CompositeSearchBehavior.LocalCatalogs;
            var connected = manager.CreateCompositePackageCatalog(options).Connect();
            if (connected.Status != ConnectResultStatus.Ok)
                throw new InvalidOperationException("WinGet connection: " + connected.Status);
            var search = Create<FindPackagesOptions>("572DED96-9C60-4526-8F92-EE7D91D38C1A");
            search.ResultLimit = 4096;
            var found = connected.PackageCatalog.FindPackages(search);
            if (found.Status != FindPackagesResultStatus.Ok || found.WasLimitExceeded)
                throw new InvalidOperationException("WinGet search incomplete: " + found.Status);
            var packages = new List<object>();
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            // Indexed access avoids the .NET Framework WinRT enumerable projection bug.
            var matches = found.Matches;
            if (matches.Count > 4096) throw new InvalidOperationException("Too many packages");
            for (int i = 0; i < matches.Count; ++i) {
                var item = matches[i].CatalogPackage;
                if (!item.IsUpdateAvailable) continue;
                var installed = item.InstalledVersion;
                var available = item.DefaultInstallVersion;
                if (installed == null || available == null)
                    throw new InvalidOperationException("Incomplete package versions");
                var id = Bounded(item.Id);
                if (String.IsNullOrWhiteSpace(id)) throw new InvalidOperationException("Missing package ID");
                if (!seen.Add(id)) continue;
                packages.Add(new { id = id, name = Bounded(item.Name),
                    installed = Bounded(installed.Version), available = Bounded(available.Version) });
            }
            result = new { status = "ok", checked_at = DateTime.UtcNow.ToString("o"), packages = packages };
        } catch (Exception error) {
            result = new { status = "error", detail = error.Message.Substring(0, Math.Min(error.Message.Length, 2048)) };
        }
        Console.WriteLine(new JavaScriptSerializer().Serialize(result));
        return 0;
    }
}
