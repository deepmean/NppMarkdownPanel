// Managed side of the ARM64 probe.
//
// Compiled to IL (AnyCPU) and loaded by the native ARM64 probe DLL through the
// .NET Framework CLR hosting API. It reports the architecture of the runtime
// that actually executed it, which proves the managed code ran natively.

using System;
using System.IO;
using System.Runtime.InteropServices;

public class ProbeEntry
{
    public static int Init(string argument)
    {
        string arch;
        try
        {
            arch = RuntimeInformation.ProcessArchitecture.ToString();
        }
        catch (Exception ex)
        {
            arch = "unknown (" + ex.GetType().Name + ")";
        }

        string payload =
            "managed-executed=true" + Environment.NewLine +
            "clr=" + Environment.Version + Environment.NewLine +
            "framework=" + RuntimeInformation.FrameworkDescription + Environment.NewLine +
            "processArch=" + arch + Environment.NewLine +
            "is64bit=" + Environment.Is64BitProcess + Environment.NewLine +
            "assemblyLocation=" + typeof(ProbeEntry).Assembly.Location + Environment.NewLine;

        try
        {
            File.WriteAllText(argument, payload);
        }
        catch
        {
            // The return code still proves execution.
        }

        return 42;
    }
}
