// NppMarkdownPanel - ARM64 self-test entry point.
//
// Called through the native shim (ExecuteInDefaultAppDomain) during the ARM64
// build's smoke test. It reports whether the managed plugin can actually run in
// an ARM64 process: the CLR architecture, and whether the UI stacks the plugin
// depends on (WinForms, Drawing, WebView2) can be loaded.
//
// This is diagnostics only - it is never invoked by Notepad++.

using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace NppMarkdownPanel
{
    public static class Arm64SelfTest
    {
        /// <summary>
        /// Writes an environment report to <paramref name="reportPath"/>.
        /// </summary>
        /// <returns>0 when the plugin's UI dependencies are available.</returns>
        public static int Run(string reportPath)
        {
            PluginAssemblyResolver.EnsureRegistered();

            var report = new StringBuilder();
            int failures = 0;

            report.AppendLine("managed-assembly=" + typeof(Arm64SelfTest).Assembly.GetName().Name);
            report.AppendLine("assembly-location=" + SafeLocation());
            report.AppendLine("clr-version=" + Environment.Version);
            report.AppendLine("framework=" + RuntimeInformation.FrameworkDescription);
            report.AppendLine("process-arch=" + RuntimeInformation.ProcessArchitecture);
            report.AppendLine("os-arch=" + RuntimeInformation.OSArchitecture);
            report.AppendLine("is-64bit-process=" + Environment.Is64BitProcess);

            // The plugin is a WinForms app; prove the UI stack loads natively.
            // These use compile-time typeof() references: reaching this code at
            // all proves the ARM64 CLR resolved and loaded the assemblies.
            failures += ProbeType(report, "System.Windows.Forms.Form", typeof(System.Windows.Forms.Form));
            failures += ProbeType(report, "System.Drawing.Bitmap", typeof(System.Drawing.Bitmap));
            failures += ProbeType(report, "System.Windows.Forms.WebBrowser", typeof(System.Windows.Forms.WebBrowser));
            failures += ProbeType(report, "System.Windows.Forms.Control", typeof(System.Windows.Forms.Control));

            // WebView2 is the default renderer, so its managed assemblies must load.
            failures += ProbeFile(report, "Microsoft.Web.WebView2.Core.dll",
                                  "Microsoft.Web.WebView2.Core.CoreWebView2Environment");
            failures += ProbeFile(report, "Microsoft.Web.WebView2.WinForms.dll",
                                  "Microsoft.Web.WebView2.WinForms.WebView2");

            // The markdown pipeline is what the panel actually renders with.
            failures += ProbeFile(report, "Markdig.dll", "Markdig.Markdown");
            failures += ProbeFile(report, "MarkdigWrapper.dll", "MarkdigWrapper.MarkdigMarkdownGenerator");
            failures += ProbeFile(report, "HtmlSanitizer.dll", "Ganss.Xss.HtmlSanitizer");

            report.AppendLine("failures=" + failures);

            try
            {
                File.WriteAllText(reportPath, report.ToString());
            }
            catch (Exception ex)
            {
                report.AppendLine("report-write-failed=" + ex.Message);
            }

            return failures == 0 ? 0 : 1;
        }

        private static string SafeLocation()
        {
            try { return typeof(Arm64SelfTest).Assembly.Location; }
            catch (Exception ex) { return "<" + ex.GetType().Name + ">"; }
        }

        private static int ProbeType(StringBuilder report, string label, Type type)
        {
            if (type == null)
            {
                report.AppendLine("probe=" + label + " -> NOT-FOUND");
                return 1;
            }

            report.AppendLine("probe=" + label + " -> OK (" + type.Assembly.GetName().Name + ")");
            return 0;
        }

        private static int ProbeFile(StringBuilder report, string assemblyFile, string typeName)
        {
            string dir = Path.GetDirectoryName(SafeLocation()) ?? ".";
            string libDir = Path.Combine(dir, "lib");
            string candidate = Path.Combine(libDir, assemblyFile);

            if (!File.Exists(candidate))
            {
                report.AppendLine("probe=" + assemblyFile + " -> FILE-MISSING (" + candidate + ")");
                return 1;
            }

            try
            {
                Assembly asm = Assembly.LoadFrom(candidate);
                Type t = asm.GetType(typeName, throwOnError: false);
                if (t == null)
                {
                    report.AppendLine("probe=" + assemblyFile + " -> TYPE-MISSING (" + typeName + ")");
                    return 1;
                }
                report.AppendLine("probe=" + assemblyFile + " -> OK");
                return 0;
            }
            catch (Exception ex)
            {
                report.AppendLine("probe=" + assemblyFile + " -> FAILED (" + ex.GetType().Name + ": " + ex.Message + ")");
                return 1;
            }
        }
    }
}
