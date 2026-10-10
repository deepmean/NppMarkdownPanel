using Kbg.NppPluginNET.PluginInfrastructure;
using System.IO;
using System.Reflection;
using System;
using System.Linq;

namespace NppMarkdownPanel
{
    class Main
    {

        // PluginName is used as npp plugin's menu entry
        public const string PluginName = "MarkdownPanel";
        // Modulename is used as config name (ini-file) and as _nppTbData.pszModuleName
        public const string ModuleName = "NppMarkdownPanel";
        public const string PluginTitle = "Markdown Panel";

        // The controller must not be built in a static field initializer.
        //
        // This class declares no explicit static constructor, so the compiler
        // marks it beforefieldinit: the runtime may execute the initializer at
        // any point before the first static field access, including while
        // JIT-compiling a method that merely references this type.
        // MarkdownPanelController's constructor reads PluginBase.nppData to
        // locate the plugin config directory, so building it before setInfo
        // delivered the handle produced:
        //
        //   Directory.CreateDirectory("") -> ArgumentException
        //   -> TypeInitializationException on Main (cached and rethrown)
        //   -> Notepad++ reports "Failed to load"
        //
        // Lazy<T> defers construction to first use, which is always after
        // setInfo, removing the ordering dependency.
        private static readonly Lazy<MarkdownPanelController> mdpanelHolder =
            new Lazy<MarkdownPanelController>(() => new MarkdownPanelController());

        private static MarkdownPanelController mdpanel
        {
            get { return mdpanelHolder.Value; }
        }

        public static void OnNotification(ScNotification notification)
        {
            mdpanel.OnNotification(notification);
        }

        internal static void CommandMenuInit()
        {
            mdpanel.InitCommandMenu();
        }

        internal static void SetToolBarIcon()
        {
            mdpanel.SetToolBarIcon();
        }

        internal static void PluginCleanUp()
        {
            // Do not force construction just to tear down. If the panel was
            // never opened there is nothing to clean up, and building a
            // controller during shutdown would need handles that may already be
            // gone.
            if (!mdpanelHolder.IsValueCreated)
                return;

            mdpanel.PluginCleanUp();
            mdpanel.Dispose();

        }



    }
}
