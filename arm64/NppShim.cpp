// NppMarkdownPanel - native ARM64 shim for Notepad++.
//
// Notepad++ loads plugins as native DLLs and resolves six unmanaged entry
// points (isUnicode, setInfo, getName, getFuncsArray, beNotified,
// messageProc). The managed plugin code is C# (.NET Framework), so this shim
// hosts the CLR and forwards each entry point to the managed implementation.
//
// Why this exists: the export layer the x86/x64 builds use
// (UnmanagedExports / RGiesecke DllExport) has no ARM64 support at all - it
// only emits I386/AMD64/IA64 stubs. This shim replaces it for ARM64.
//
// Design notes:
//   * No extra runtime dependency. .NET Framework 4.8.1 ships natively on
//     Windows 11 ARM64, so users install nothing.
//   * The managed assembly is loaded by *full path* via
//     ExecuteInDefaultAppDomain, so it may live in the plugin subfolder
//     (plugins\NppMarkdownPanel\) instead of next to notepad++.exe. This is
//     what makes the Notepad++ plugin layout work.
//   * The three NppData handles are forwarded to managed code as separate
//     pointers, avoiding a large-composite struct ABI mismatch.

#include <Windows.h>
#include <mscoree.h>
#include <metahost.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

#pragma comment(lib, "mscoree.lib")
// MessageBoxW (failure reporting) and registry-free Win32 helpers.
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------------------
// Notepad++ plugin ABI
// ---------------------------------------------------------------------------

struct NppData
{
    void* _nppHandle;
    void* _scintillaMainHandle;
    void* _scintillaSecondHandle;
};

typedef BOOL(__cdecl* FnIsUnicode)();
typedef void(__cdecl* FnSetInfo)(void*, void*, void*);
typedef const wchar_t* (__cdecl* FnGetName)();
typedef void* (__cdecl* FnGetFuncsArray)(int*);
typedef void(__cdecl* FnBeNotified)(void*);
typedef LRESULT(__cdecl* FnMessageProc)(UINT, WPARAM, LPARAM);

// Slot order is a contract with Arm64Entry.Bootstrap in the managed side.
enum ExportSlot
{
    SlotIsUnicode = 0,
    SlotSetInfo = 1,
    SlotGetName = 2,
    SlotGetFuncsArray = 3,
    SlotBeNotified = 4,
    SlotMessageProc = 5,
    SlotCount = 6
};

// Managed type that publishes the export table.
#define MANAGED_ENTRY_TYPE L"NppMarkdownPanel.Arm64Entry"
#define MANAGED_ENTRY_METHOD L"Bootstrap"
#define MANAGED_ASSEMBLY_NAME L"NppMarkdownPanel.Managed.dll"

// Managed diagnostics entry point, used by the build's smoke test only.
#define MANAGED_SELFTEST_TYPE L"NppMarkdownPanel.Arm64SelfTest"
#define MANAGED_SELFTEST_METHOD L"Run"

namespace
{
    HMODULE g_self = nullptr;
    ICLRRuntimeHost* g_host = nullptr;
    bool g_ready = false;
    bool g_failed = false;
    wchar_t g_error[1024] = L"";

    struct ExportTable
    {
        FnIsUnicode isUnicode;
        FnSetInfo setInfo;
        FnGetName getName;
        FnGetFuncsArray getFuncsArray;
        FnBeNotified beNotified;
        FnMessageProc messageProc;
    } g_table = {};

    void SetError(const wchar_t* format, ...)
    {
        va_list args;
        va_start(args, format);
        _vsnwprintf_s(g_error, _countof(g_error), _TRUNCATE, format, args);
        va_end(args);
    }

    // Path of the managed assembly that sits next to this shim.
    bool GetManagedAssemblyPath(wchar_t* out, size_t count)
    {
        wchar_t self[MAX_PATH];
        DWORD written = GetModuleFileNameW(g_self, self, MAX_PATH);
        if (written == 0 || written >= MAX_PATH)
        {
            SetError(L"GetModuleFileNameW failed (%lu)", GetLastError());
            return false;
        }

        wchar_t* slash = wcsrchr(self, L'\\');
        if (slash == nullptr)
        {
            SetError(L"unexpected module path: %s", self);
            return false;
        }
        *(slash + 1) = L'\0';

        if (wcscpy_s(out, count, self) != 0 ||
            wcscat_s(out, count, MANAGED_ASSEMBLY_NAME) != 0)
        {
            SetError(L"managed assembly path too long");
            return false;
        }
        return true;
    }

    bool StartClr()
    {
        ICLRMetaHost* meta = nullptr;
        HRESULT hr = CLRCreateInstance(CLSID_CLRMetaHost, IID_ICLRMetaHost,
                                       reinterpret_cast<void**>(&meta));
        if (FAILED(hr) || meta == nullptr)
        {
            SetError(L"CLRCreateInstance failed (0x%08X)", static_cast<unsigned>(hr));
            return false;
        }

        ICLRRuntimeInfo* info = nullptr;
        hr = meta->GetRuntime(L"v4.0.30319", IID_ICLRRuntimeInfo,
                              reinterpret_cast<void**>(&info));
        if (FAILED(hr) || info == nullptr)
        {
            meta->Release();
            SetError(L"GetRuntime v4.0.30319 failed (0x%08X). "
                     L".NET Framework 4.8.1 or later is required on Windows on ARM64.",
                     static_cast<unsigned>(hr));
            return false;
        }

        hr = info->GetInterface(CLSID_CLRRuntimeHost, IID_ICLRRuntimeHost,
                                reinterpret_cast<void**>(&g_host));
        info->Release();
        meta->Release();

        if (FAILED(hr) || g_host == nullptr)
        {
            SetError(L"GetInterface(ICLRRuntimeHost) failed (0x%08X)", static_cast<unsigned>(hr));
            return false;
        }

        hr = g_host->Start();
        if (FAILED(hr))
        {
            SetError(L"ICLRRuntimeHost::Start failed (0x%08X)", static_cast<unsigned>(hr));
            return false;
        }

        return true;
    }

    // Path of the managed assembly, with a one-time diagnostic on failure.
    bool ResolveManagedPath(wchar_t* managed, size_t count)
    {
        if (!GetManagedAssemblyPath(managed, count))
            return false;

        if (GetFileAttributesW(managed) == INVALID_FILE_ATTRIBUTES)
        {
            SetError(L"managed assembly not found: %s (error %lu)", managed, GetLastError());
            return false;
        }
        return true;
    }

    bool EnsureReady()
    {
        if (g_ready)
            return true;
        if (g_failed)
            return false;

        wchar_t managed[MAX_PATH * 2];
        if (!ResolveManagedPath(managed, _countof(managed)))
        {
            g_failed = true;
            return false;
        }

        if (GetFileAttributesW(managed) == INVALID_FILE_ATTRIBUTES)
        {
            SetError(L"managed assembly not found: %s (error %lu)", managed, GetLastError());
            g_failed = true;
            return false;
        }

        if (!StartClr())
        {
            g_failed = true;
            return false;
        }

        // Ask the managed side to publish its export table into this buffer.
        void* table[SlotCount] = {};
        wchar_t argument[64];
        swprintf_s(argument, L"%llX",
                   static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(table)));

        DWORD managedReturn = 0xFFFFFFFF;
        HRESULT hr = g_host->ExecuteInDefaultAppDomain(managed,
                                                       MANAGED_ENTRY_TYPE,
                                                       MANAGED_ENTRY_METHOD,
                                                       argument,
                                                       &managedReturn);
        if (FAILED(hr))
        {
            SetError(L"ExecuteInDefaultAppDomain failed (0x%08X). Managed assembly: %s",
                     static_cast<unsigned>(hr), managed);
            g_failed = true;
            return false;
        }
        if (managedReturn != 0)
        {
            SetError(L"managed Bootstrap returned %lu for %s",
                     static_cast<unsigned long>(managedReturn), managed);
            g_failed = true;
            return false;
        }

        for (int i = 0; i < SlotCount; ++i)
        {
            if (table[i] == nullptr)
            {
                SetError(L"managed export slot %d is null", i);
                g_failed = true;
                return false;
            }
        }

        g_table.isUnicode = reinterpret_cast<FnIsUnicode>(table[SlotIsUnicode]);
        g_table.setInfo = reinterpret_cast<FnSetInfo>(table[SlotSetInfo]);
        g_table.getName = reinterpret_cast<FnGetName>(table[SlotGetName]);
        g_table.getFuncsArray = reinterpret_cast<FnGetFuncsArray>(table[SlotGetFuncsArray]);
        g_table.beNotified = reinterpret_cast<FnBeNotified>(table[SlotBeNotified]);
        g_table.messageProc = reinterpret_cast<FnMessageProc>(table[SlotMessageProc]);

        g_ready = true;
        return true;
    }

    // Report a bootstrap failure once, then let the plugin degrade quietly so
    // Notepad++ itself keeps working.
    void ReportFailureOnce()
    {
        static bool reported = false;
        if (reported)
            return;
        reported = true;

        wchar_t message[1600];
        _snwprintf_s(message, _countof(message), _TRUNCATE,
                     L"MarkdownPanel (ARM64) could not start.\n\n%s", g_error);
        MessageBoxW(nullptr, message, L"MarkdownPanel", MB_OK | MB_ICONERROR);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_self = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// Notepad++ entry points
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport) BOOL isUnicode()
{
    if (!EnsureReady())
    {
        ReportFailureOnce();
        return TRUE; // Claim Unicode support so Notepad++ keeps the plugin loaded.
    }
    return g_table.isUnicode() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) void setInfo(NppData data)
{
    if (!EnsureReady())
    {
        ReportFailureOnce();
        return;
    }
    g_table.setInfo(data._nppHandle, data._scintillaMainHandle, data._scintillaSecondHandle);
}

extern "C" __declspec(dllexport) const wchar_t* getName()
{
    if (!EnsureReady())
        return L"MarkdownPanel";
    return g_table.getName();
}

extern "C" __declspec(dllexport) void* getFuncsArray(int* nbF)
{
    if (nbF != nullptr)
        *nbF = 0;
    if (!EnsureReady())
        return nullptr;
    return g_table.getFuncsArray(nbF);
}

extern "C" __declspec(dllexport) void beNotified(void* notifyCode)
{
    if (!EnsureReady())
        return;
    g_table.beNotified(notifyCode);
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!EnsureReady())
        return 1;
    return g_table.messageProc(message, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Diagnostics (not part of the Notepad++ contract)
// ---------------------------------------------------------------------------

// Runs NppMarkdownPanel.Arm64SelfTest.Run(reportPath) and returns its exit
// code, or a negative value when the shim itself could not start. Used by the
// ARM64 build's smoke test to verify the managed plugin can load in an ARM64
// process.
extern "C" __declspec(dllexport) int arm64SelfTest(const wchar_t* reportPath)
{
    wchar_t managed[MAX_PATH * 2];
    if (!ResolveManagedPath(managed, _countof(managed)))
        return -1;

    if (!StartClr())
        return -2;

    DWORD managedReturn = 0xFFFFFFFF;
    HRESULT hr = g_host->ExecuteInDefaultAppDomain(managed,
                                                   MANAGED_SELFTEST_TYPE,
                                                   MANAGED_SELFTEST_METHOD,
                                                   reportPath,
                                                   &managedReturn);
    if (FAILED(hr))
    {
        SetError(L"self-test failed to invoke managed code (0x%08X)", static_cast<unsigned>(hr));
        return -3;
    }

    return static_cast<int>(managedReturn);
}

// Returns the last shim-side error message, or an empty string.
extern "C" __declspec(dllexport) const wchar_t* arm64LastError()
{
    return g_error;
}
