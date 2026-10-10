// ARM64 probe DLL for NppMarkdownPanel.
//
// Proves that a native ARM64 DLL can host the .NET Framework v4 CLR and load a
// managed assembly from an arbitrary directory - the layout Notepad++ plugins
// use (plugins\<Name>\...). No .NET 8 or any other runtime is required.

#include <Windows.h>
#include <mscoree.h>
#include <metahost.h>

namespace
{
    ICLRRuntimeHost* g_host = nullptr;

    // Start the v4 CLR once and cache the runtime host.
    HRESULT EnsureRuntime()
    {
        if (g_host != nullptr)
            return S_OK;

        ICLRMetaHost* meta = nullptr;
        HRESULT hr = CLRCreateInstance(CLSID_CLRMetaHost, IID_ICLRMetaHost,
                                       reinterpret_cast<void**>(&meta));
        if (FAILED(hr))
            return hr;

        ICLRRuntimeInfo* info = nullptr;
        hr = meta->GetRuntime(L"v4.0.30319", IID_ICLRRuntimeInfo,
                              reinterpret_cast<void**>(&info));
        if (SUCCEEDED(hr))
        {
            hr = info->GetInterface(CLSID_CLRRuntimeHost, IID_ICLRRuntimeHost,
                                    reinterpret_cast<void**>(&g_host));
            if (SUCCEEDED(hr))
                hr = g_host->Start();

            info->Release();
        }

        meta->Release();
        return hr;
    }
}

// Returns 0 when the CLR was created and started successfully.
extern "C" __declspec(dllexport) int probe_host(void)
{
    HRESULT hr = EnsureRuntime();
    return SUCCEEDED(hr) ? 0 : static_cast<int>(hr);
}

// Loads the managed assembly from an explicit full path and invokes
// ProbeEntry.Init(string). Returns the managed return value, or a negative
// HRESULT on failure.
extern "C" __declspec(dllexport) int probe_exec(const wchar_t* assemblyPath,
                                                const wchar_t* argument,
                                                int* managedResult)
{
    HRESULT hr = EnsureRuntime();
    if (FAILED(hr))
        return static_cast<int>(hr);

    DWORD ret = 0;
    hr = g_host->ExecuteInDefaultAppDomain(assemblyPath,
                                           L"ProbeEntry",
                                           L"Init",
                                           argument,
                                           &ret);
    if (FAILED(hr))
        return static_cast<int>(hr);

    if (managedResult != nullptr)
        *managedResult = static_cast<int>(ret);

    return 0;
}
