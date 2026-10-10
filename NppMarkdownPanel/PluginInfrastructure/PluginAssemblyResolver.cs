// NppMarkdownPanel - assembly resolver for the ARM64 build.
//
// Why this is needed: the ARM64 shim loads the managed plugin from
// <notepad++>\plugins\NppMarkdownPanel\NppMarkdownPanel.Managed.dll. The
// .NET Framework probing rules use the *host* EXE's directory as the
// application base, so the plugin's own dependencies (lib\*.dll) are not found
// automatically. Without this hook the very first call into managed code would
// fail with a FileNotFoundException for MarkdigWrapper/PanelCommon/etc.
//
// The x86/x64 build has the same shape and relies on a handler registered in
// MarkdownPanelController's constructor. That is too late for ARM64: the JIT
// may need those assemblies while preparing the entry point itself, before any
// controller is constructed. Registering here - at the earliest managed entry -
// makes the layout work regardless of JIT ordering.

using System;
using System.IO;
using System.Reflection;

namespace NppMarkdownPanel
{
    internal static class PluginAssemblyResolver
    {
        private static readonly object SyncRoot = new object();
        private static bool _registered;
        private static string _pluginDirectory;

        /// <summary>
        /// Registers the resolver once, if it is not already active.
        /// Safe to call from any managed entry point.
        /// </summary>
        internal static void EnsureRegistered()
        {
            if (_registered)
                return;

            lock (SyncRoot)
            {
                if (_registered)
                    return;

                AppDomain.CurrentDomain.AssemblyResolve += Resolve;
                _registered = true;
            }
        }

        private static Assembly Resolve(object sender, ResolveEventArgs args)
        {
            string simpleName = new AssemblyName(args.Name).Name;
            if (string.IsNullOrEmpty(simpleName))
                return null;

            foreach (string directory in CandidateDirectories())
            {
                string candidate = Path.Combine(directory, simpleName + ".dll");
                if (File.Exists(candidate))
                {
                    try
                    {
                        return Assembly.LoadFrom(candidate);
                    }
                    catch (Exception)
                    {
                        // Fall through to the next candidate directory; a broken
                        // assembly here should not mask a good one elsewhere.
                    }
                }
            }

            return null;
        }

        private static string[] CandidateDirectories()
        {
            if (_pluginDirectory == null)
            {
                try
                {
                    // The managed assembly lives in the plugin folder itself.
                    _pluginDirectory = Path.GetDirectoryName(
                        Assembly.GetExecutingAssembly().Location) ?? string.Empty;
                }
                catch (Exception)
                {
                    _pluginDirectory = string.Empty;
                }
            }

            return new[]
            {
                Path.Combine(_pluginDirectory, "lib"),
                _pluginDirectory
            };
        }
    }
}
