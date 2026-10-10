// NppMarkdownPanel - ARM64 smoke test harness.
//
// Loads the built plugin shim the way Notepad++ does (LoadLibraryEx with
// LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR) and resolves the six entry points
// Notepad++ requires. It then crosses the managed boundary for real, which a
// plain compile cannot demonstrate.
//
// It deliberately does NOT call setInfo: that would initialise the plugin's
// window controller, which needs a live Notepad++ window handle.

#include <Windows.h>
#include <stdio.h>
#include <string>

typedef BOOL(__cdecl* FnIsUnicode)();
typedef const wchar_t* (__cdecl* FnGetName)();
typedef void* (__cdecl* FnGetFuncsArray)(int*);
typedef LRESULT(__cdecl* FnMessageProc)(UINT, WPARAM, LPARAM);
typedef int(__cdecl* FnSelfTest)(const wchar_t*);
typedef const wchar_t* (__cdecl* FnLastError)();

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3)
    {
        wprintf(L"usage: NppArm64SmokeTest <plugin folder> <report path>\n");
        return 2;
    }

    const std::wstring folder = argv[1];
    const wchar_t* reportPath = argv[2];

    wchar_t dll[MAX_PATH * 2];
    if (swprintf_s(dll, L"%s\\NppMarkdownPanel.dll", folder.c_str()) < 0)
    {
        wprintf(L"path too long\n");
        return 2;
    }
    wprintf(L"shim: %s\n", dll);

    // Same flags Notepad++ uses so the shim resolves its dependencies from its
    // own folder, exactly as it will in the real host.
    const DWORD flags = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "AddDllDirectory") != nullptr
                            ? LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
                            : 0;

    HMODULE dllHandle = LoadLibraryExW(dll, nullptr, flags);
    if (dllHandle == nullptr)
    {
        wprintf(L"FAIL: LoadLibraryEx failed (%lu)\n", GetLastError());
        return 3;
    }
    wprintf(L"LoadLibraryEx: OK\n\n");

    int failures = 0;

    FnIsUnicode isUnicode = reinterpret_cast<FnIsUnicode>(GetProcAddress(dllHandle, "isUnicode"));
    FnGetName getName = reinterpret_cast<FnGetName>(GetProcAddress(dllHandle, "getName"));
    FnGetFuncsArray getFuncsArray = reinterpret_cast<FnGetFuncsArray>(GetProcAddress(dllHandle, "getFuncsArray"));
    FnMessageProc messageProc = reinterpret_cast<FnMessageProc>(GetProcAddress(dllHandle, "messageProc"));
    void* setInfo = reinterpret_cast<void*>(GetProcAddress(dllHandle, "setInfo"));
    void* beNotified = reinterpret_cast<void*>(GetProcAddress(dllHandle, "beNotified"));
    FnLastError lastError = reinterpret_cast<FnLastError>(GetProcAddress(dllHandle, "arm64LastError"));

    struct { const char* name; const void* address; } required[] = {
        { "isUnicode", reinterpret_cast<const void*>(isUnicode) },
        { "setInfo", setInfo },
        { "getName", reinterpret_cast<const void*>(getName) },
        { "getFuncsArray", reinterpret_cast<const void*>(getFuncsArray) },
        { "beNotified", beNotified },
        { "messageProc", reinterpret_cast<const void*>(messageProc) },
    };

    wprintf(L"--- Notepad++ entry points ---\n");
    for (const auto& entry : required)
    {
        bool ok = entry.address != nullptr;
        wprintf(L"export %-16S %s\n", entry.name, ok ? L"OK" : L"MISSING");
        if (!ok)
            ++failures;
    }

    // Calling isUnicode() is the first real crossing of the managed boundary:
    // it forces the shim to start the CLR, load the managed assembly and
    // resolve the export table.
    wprintf(L"\n--- managed boundary ---\n");
    if (isUnicode != nullptr)
    {
        BOOL result = isUnicode();
        wprintf(L"isUnicode() -> %d %s\n", result, result ? L"(OK)" : L"(FAIL)");
        if (!result)
            ++failures;
    }

    if (getName != nullptr)
    {
        const wchar_t* name = getName();
        wprintf(L"getName() -> %s\n", name != nullptr ? name : L"<null>");
        if (name == nullptr || *name == L'\0')
            ++failures;
    }

    // getFuncsArray is only populated once setInfo has run CommandMenuInit, and
    // the smoke test deliberately never calls setInfo (it would need a live
    // Notepad++ window handle). An empty array here is correct, not a failure -
    // the point of this call is that it crosses into managed code and returns.
    if (getFuncsArray != nullptr)
    {
        int count = -1;
        void* items = getFuncsArray(&count);
        wprintf(L"getFuncsArray() -> %p, %d item(s) (empty is expected before setInfo)\n", items, count);
    }

    if (messageProc != nullptr)
    {
        LRESULT result = messageProc(0, 0, 0);
        wprintf(L"messageProc() -> %lld\n", static_cast<long long>(result));
    }

    // The managed plugin's own diagnostics: WinForms, Drawing, WebView2 and the
    // markdown pipeline, loaded from the plugin's lib folder.
    FnSelfTest selfTest = reinterpret_cast<FnSelfTest>(GetProcAddress(dllHandle, "arm64SelfTest"));
    if (selfTest != nullptr)
    {
        wprintf(L"\n--- managed self test ---\n");
        int rc = selfTest(reportPath);
        wprintf(L"arm64SelfTest() -> %d\n", rc);
        if (rc != 0)
            ++failures;
    }
    else
    {
        wprintf(L"\nnote: arm64SelfTest export not found\n");
        ++failures;
    }

    if (failures != 0 && lastError != nullptr)
    {
        const wchar_t* message = lastError();
        if (message != nullptr && *message != L'\0')
            wprintf(L"\nshim error: %s\n", message);
    }

    wprintf(L"\n%s (%d failure(s))\n", failures == 0 ? L"SMOKE TEST: PASS" : L"SMOKE TEST: FAIL", failures);
    return failures == 0 ? 0 : 1;
}
