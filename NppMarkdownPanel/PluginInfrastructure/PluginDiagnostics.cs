// NppMarkdownPanel - ARM64 startup diagnostics.
//
// Writes a step-by-step trace next to the plugin DLL so failures that
// Notepad++ only reports as "Failed to load" can be diagnosed from the actual
// in-process exception instead of guessed at.
//
// The x86/x64 build has no equivalent: its failures are observable under a
// debugger attached to Notepad++. The ARM64 build crosses an extra native
// boundary (shim -> CLR -> managed), so a file trace is the practical way to
// see where that handoff breaks.
//
// Logging is best-effort: a failure to write must never itself break plugin
// loading.

using System;
using System.IO;
using System.Reflection;
using System.Text;

namespace NppMarkdownPanel
{
    internal static class PluginDiagnostics
    {
        private static readonly object SyncRoot = new object();
        private static string _logPath;
        private static bool _logPathResolved;

        /// <summary>
        /// Absolute path of the trace file, or null when it cannot be resolved.
        /// </summary>
        /// <remarks>
        /// Prefers the plugin folder so the log sits next to the plugin it
        /// describes. Falls back to the temp directory because Notepad++ is
        /// often installed under Program Files, where the plugin folder is not
        /// writable without elevation - without the fallback, diagnostics would
        /// silently disappear in exactly the deployments that need them.
        /// </remarks>
        internal static string LogPath
        {
            get
            {
                if (!_logPathResolved)
                {
                    _logPathResolved = true;
                    _logPath = ResolveLogPath();
                }
                return _logPath;
            }
        }

        private static string ResolveLogPath()
        {
            string pluginDir = null;
            try
            {
                pluginDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            }
            catch (Exception)
            {
                // Fall through to the temp directory.
            }

            string candidate = TryDirectory(pluginDir);
            if (candidate != null)
                return candidate;

            try
            {
                return Path.Combine(Path.GetTempPath(), "NppMarkdownPanel-arm64.log");
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static string TryDirectory(string directory)
        {
            if (string.IsNullOrEmpty(directory))
                return null;

            string candidate = Path.Combine(directory, "arm64-startup.log");
            try
            {
                // Probing with an append keeps this cheap and proves the
                // directory is writable before anything important is lost.
                using (FileStream probe = new FileStream(candidate, FileMode.Append,
                                                         FileAccess.Write, FileShare.ReadWrite))
                {
                    probe.Flush();
                }
                return candidate;
            }
            catch (Exception)
            {
                return null;
            }
        }

        internal static void Log(string message)
        {
            try
            {
                string path = LogPath;
                if (path == null)
                    return;

                lock (SyncRoot)
                {
                    File.AppendAllText(path,
                        DateTime.Now.ToString("HH:mm:ss.fff") + "  " + message + Environment.NewLine,
                        Encoding.UTF8);
                }
            }
            catch (Exception)
            {
                // Diagnostics must never break plugin loading.
            }
        }

        internal static void LogException(string context, Exception ex)
        {
            if (ex == null)
            {
                Log(context + " : <null exception>");
                return;
            }

            var sb = new StringBuilder();
            sb.Append(context);
            sb.Append(" : ");
            sb.Append(ex.GetType().FullName);
            sb.Append(" : ");
            sb.Append(ex.Message);

            Exception inner = ex.InnerException;
            int depth = 1;
            while (inner != null && depth <= 6)
            {
                sb.Append(" | inner").Append(depth).Append('=');
                sb.Append(inner.GetType().Name).Append(": ").Append(inner.Message);
                if (inner.StackTrace != null)
                    sb.Append(" @ ").Append(FirstFrame(inner.StackTrace));
                inner = inner.InnerException;
                depth++;
            }

            Log(sb.ToString());

            // Full stacks, so the failing frame is visible even when the
            // message alone is ambiguous.
            Log("  stack: " + ex.StackTrace);

            Exception walk = ex.InnerException;
            int level = 1;
            while (walk != null && level <= 6)
            {
                Log("  inner" + level + " stack: " + walk.StackTrace);
                walk = walk.InnerException;
                level++;
            }
        }

        private static string FirstFrame(string stackTrace)
        {
            if (string.IsNullOrEmpty(stackTrace))
                return "<no stack>";

            string[] lines = stackTrace.Split('\n');
            for (int i = 0; i < lines.Length; i++)
            {
                string line = lines[i].Trim();
                if (line.Length == 0)
                    continue;
                // Skip the reflection/CLR frames that only add noise.
                if (line.StartsWith("at System.RuntimeMethodHandle", StringComparison.Ordinal) ||
                    line.StartsWith("at System.Reflection", StringComparison.Ordinal) ||
                    line.StartsWith("at System.RuntimeType", StringComparison.Ordinal))
                    continue;
                return line;
            }
            return "<no user frame>";
        }
    }
}
