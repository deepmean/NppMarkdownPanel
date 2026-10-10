// NppMarkdownPanel - managed entry point for the native ARM64 shim.
//
// The x86/x64 builds export the Notepad++ entry points directly from this
// assembly using UnmanagedExports / RGiesecke DllExport. That tooling has no
// ARM64 support, so on ARM64 a native shim DLL (NppMarkdownPanel.dll) exports
// them instead and forwards each call here.
//
// This type is only compiled for the ARM64 configuration; UnmanagedExports.cs
// is excluded there. The two are mutually exclusive entry layers.

using System;
using System.Globalization;
using System.Runtime.InteropServices;
using Kbg.NppPluginNET.PluginInfrastructure;

namespace NppMarkdownPanel
{
    /// <summary>
    /// Receives a pointer to a table of six function-pointer slots from the
    /// native shim and fills it in. The slot order is a contract with
    /// <c>ExportSlot</c> in arm64/NppShim.cpp.
    /// </summary>
    public static class Arm64Entry
    {
        // Reverse-P/Invoke stubs. These delegates must outlive every call made
        // by the shim, so they are held in static readonly fields.
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int IsUnicodeDelegate();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void SetInfoDelegate(IntPtr nppHandle, IntPtr scintillaMain, IntPtr scintillaSecond);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr GetNameDelegate();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr GetFuncsArrayDelegate(IntPtr nbFuncItems);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void BeNotifiedDelegate(IntPtr notification);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr MessageProcDelegate(uint message, IntPtr wParam, IntPtr lParam);

        private static readonly IsUnicodeDelegate _isUnicode = IsUnicodeImpl;
        private static readonly SetInfoDelegate _setInfo = SetInfoImpl;
        private static readonly GetNameDelegate _getName = GetNameImpl;
        private static readonly GetFuncsArrayDelegate _getFuncsArray = GetFuncsArrayImpl;
        private static readonly BeNotifiedDelegate _beNotified = BeNotifiedImpl;
        private static readonly MessageProcDelegate _messageProc = MessageProcImpl;

        private static IntPtr _ptrPluginName = IntPtr.Zero;

        /// <summary>
        /// Called by the native shim through ExecuteInDefaultAppDomain. The
        /// argument is the address of the shim's export table, as hex.
        /// </summary>
        /// <returns>0 on success; a non-zero code describes the failure.</returns>
        public static int Bootstrap(string exportTableAddress)
        {
            // Must run before anything else: the plugin's dependencies live in
            // lib\ and are not on the CLR's default probing path when the host
            // process is notepad++.exe.
            PluginAssemblyResolver.EnsureRegistered();

            if (string.IsNullOrEmpty(exportTableAddress))
                return 1;

            long address;
            if (!long.TryParse(exportTableAddress, NumberStyles.HexNumber,
                               CultureInfo.InvariantCulture, out address))
                return 2;

            IntPtr table = new IntPtr(address);
            if (table == IntPtr.Zero)
                return 3;

            int slot = IntPtr.Size;
            Marshal.WriteIntPtr(table, 0 * slot, Marshal.GetFunctionPointerForDelegate(_isUnicode));
            Marshal.WriteIntPtr(table, 1 * slot, Marshal.GetFunctionPointerForDelegate(_setInfo));
            Marshal.WriteIntPtr(table, 2 * slot, Marshal.GetFunctionPointerForDelegate(_getName));
            Marshal.WriteIntPtr(table, 3 * slot, Marshal.GetFunctionPointerForDelegate(_getFuncsArray));
            Marshal.WriteIntPtr(table, 4 * slot, Marshal.GetFunctionPointerForDelegate(_beNotified));
            Marshal.WriteIntPtr(table, 5 * slot, Marshal.GetFunctionPointerForDelegate(_messageProc));

            PluginDiagnostics.Log("Bootstrap OK (export table published)");
            return 0;
        }

        private static int IsUnicodeImpl()
        {
            return 1;
        }

        private static void SetInfoImpl(IntPtr nppHandle, IntPtr scintillaMain, IntPtr scintillaSecond)
        {
            PluginBase.nppData = new NppData
            {
                _nppHandle = nppHandle,
                _scintillaMainHandle = scintillaMain,
                _scintillaSecondHandle = scintillaSecond
            };

            // Main.CommandMenuInit() builds the panel controller, which needs
            // the handles stored just above to resolve the plugin config
            // directory. Catch and log here because the native shim cannot see
            // managed exception detail, and Notepad++ only reports a generic
            // "Failed to load".
            try
            {
                Main.CommandMenuInit();
                PluginDiagnostics.Log("setInfo OK: controller built, npp=0x" + nppHandle.ToInt64().ToString("X"));
            }
            catch (Exception ex)
            {
                PluginDiagnostics.LogException("setInfo FAILED", ex);
                throw;
            }
        }

        private static IntPtr GetNameImpl()
        {
            if (_ptrPluginName == IntPtr.Zero)
                _ptrPluginName = Marshal.StringToHGlobalUni(Main.PluginName);

            return _ptrPluginName;
        }

        private static IntPtr GetFuncsArrayImpl(IntPtr nbFuncItems)
        {
            try
            {
                int count = PluginBase._funcItems.Items.Count;
                if (nbFuncItems != IntPtr.Zero)
                    Marshal.WriteInt32(nbFuncItems, count);

                PluginDiagnostics.Log("getFuncsArray OK: " + count + " menu item(s)");
                return PluginBase._funcItems.NativePointer;
            }
            catch (Exception ex)
            {
                PluginDiagnostics.LogException("getFuncsArray FAILED", ex);
                throw;
            }
        }

        private static void BeNotifiedImpl(IntPtr notifyCode)
        {
            try
            {
                ScNotification notification = (ScNotification)Marshal.PtrToStructure(notifyCode, typeof(ScNotification));

                if (notification.Header.Code == (uint)NppMsg.NPPN_TBMODIFICATION)
                {
                    PluginBase._funcItems.RefreshItems();
                    Main.SetToolBarIcon();
                }
                else if (notification.Header.Code == (uint)NppMsg.NPPN_SHUTDOWN)
                {
                    Main.PluginCleanUp();
                    Marshal.FreeHGlobal(_ptrPluginName);
                    _ptrPluginName = IntPtr.Zero;
                }
                else
                {
                    Main.OnNotification(notification);
                }
            }
            catch (Exception ex)
            {
                PluginDiagnostics.LogException("beNotified FAILED", ex);
            }
        }

        private static IntPtr MessageProcImpl(uint message, IntPtr wParam, IntPtr lParam)
        {
            return new IntPtr(1);
        }
    }
}
