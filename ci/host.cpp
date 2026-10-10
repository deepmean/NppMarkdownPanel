// Native ARM64 harness for the NppMarkdownPanel probe.
//
// Loads the probe DLL (ARM64), starts the .NET Framework CLR, then asks it to
// load a managed assembly from a *subdirectory* - the exact layout Notepad++
// uses for plugins (plugins\<Name>\<Name>.dll) rather than next to the EXE.

#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef int (*probe_host_fn)(void);
typedef int (*probe_exec_fn)(const wchar_t*, const wchar_t*, int*);

static void MakeSiblingPath(wchar_t* out, size_t count, const wchar_t* base,
                            const wchar_t* leaf)
{
    wcscpy_s(out, count, base);
    wchar_t* slash = wcsrchr(out, L'\\');
    if (slash != nullptr)
        *(slash + 1) = L'\0';
    wcscat_s(out, count, leaf);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
    {
        wprintf(L"usage: NppArm64Host <probe.dll>\n");
        return 2;
    }

    const wchar_t* dllPath = argv[1];
    wprintf(L"probe dll : %s\n", dllPath);

    HMODULE dll = LoadLibraryW(dllPath);
    if (dll == nullptr)
    {
        wprintf(L"LoadLibraryW failed: %lu\n", GetLastError());
        return 3;
    }

    probe_host_fn host = reinterpret_cast<probe_host_fn>(
        GetProcAddress(dll, "probe_host"));
    probe_exec_fn exec = reinterpret_cast<probe_exec_fn>(
        GetProcAddress(dll, "probe_exec"));
    if (host == nullptr || exec == nullptr)
    {
        wprintf(L"GetProcAddress failed (host=%p exec=%p)\n", host, exec);
        return 4;
    }

    int rc = host();
    wprintf(L"probe_host() -> %d\n", rc);
    if (rc != 0)
    {
        wprintf(L"RESULT: CLR HOST FAILED\n");
        return 5;
    }

    // Managed assembly lives in a subdirectory, like a Notepad++ plugin.
    wchar_t managed[1024];
    MakeSiblingPath(managed, 1024, dllPath, L"managed\\NppArm64Probe.Managed.dll");

    wchar_t report[1024];
    MakeSiblingPath(report, 1024, dllPath, L"out\\managed-report.txt");

    wprintf(L"managed   : %s\n", managed);
    wprintf(L"report    : %s\n", report);

    if (GetFileAttributesW(managed) == INVALID_FILE_ATTRIBUTES)
    {
        wprintf(L"managed assembly not found: %lu\n", GetLastError());
        return 6;
    }

    int managedResult = -1;
    rc = exec(managed, report, &managedResult);
    wprintf(L"probe_exec() -> %d (managed returned %d)\n", rc, managedResult);
    if (rc != 0)
    {
        wprintf(L"RESULT: MANAGED EXEC FAILED\n");
        return 7;
    }

    if (managedResult != 42)
    {
        wprintf(L"RESULT: UNEXPECTED MANAGED RETURN\n");
        return 8;
    }

    wprintf(L"RESULT: OK\n");
    return 0;
}
