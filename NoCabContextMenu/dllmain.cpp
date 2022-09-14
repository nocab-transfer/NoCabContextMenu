// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <atlfile.h>
#include <atlstr.h>
#include <shobjidl_core.h>
#include <string>
#include <filesystem>
#include <sstream>
#include <Shlwapi.h>
#include <vector>
#include <wil\resource.h>
#include <wil\win32_helpers.h>
#include <wil\stl.h>
#include <wrl/module.h>
#include <wrl/implements.h>
#include <wrl/client.h>
#include <mutex>
#include <thread>
#include <shellapi.h>

using namespace Microsoft::WRL;

HINSTANCE g_hInst = 0;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hInst = hModule;
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

// https://github.com/microsoft/PowerToys/blob/3443c73d0e81a958974368763631035f3e510653/src/common/utils/process_path.h
// i borrowed it from microsoft :)
inline std::wstring get_module_folderpath(HMODULE mod = nullptr, const bool removeFilename = true)
{
    wchar_t buffer[MAX_PATH + 1];
    DWORD actual_length = GetModuleFileNameW(mod, buffer, MAX_PATH);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        const DWORD long_path_length = 0xFFFF; // should be always enough
        std::wstring long_filename(long_path_length, L'\0');
        actual_length = GetModuleFileNameW(mod, long_filename.data(), long_path_length);
        PathRemoveFileSpecW(long_filename.data());
        long_filename.resize(std::wcslen(long_filename.data()));
        long_filename.shrink_to_fit();
        return long_filename;
    }

    if (removeFilename)
    {
        PathRemoveFileSpecW(buffer);
    }
    return { buffer, (UINT)lstrlenW(buffer) };
}

class __declspec(uuid("FDDDFECC-B9ED-4837-AF07-BD891CD8B6F4")) NoCabContextMenuCommand final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IExplorerCommand, IObjectWithSite>
{
public:
    virtual const wchar_t* Title() { return L"Send with NoCab"; };
    virtual const EXPCMDFLAGS Flags() { return ECF_DEFAULT; }
    virtual const EXPCMDSTATE State(_In_opt_ IShellItemArray* selection) { return ECS_ENABLED; }

    // IExplorerCommand
    IFACEMETHODIMP GetTitle(_In_opt_ IShellItemArray* items, _Outptr_result_nullonfailure_ PWSTR* name)
    {   
        *name = nullptr;
        return SHStrDupW(L"Send with NoCab", name);
    }
    IFACEMETHODIMP GetIcon(_In_opt_ IShellItemArray*, _Outptr_result_nullonfailure_ PWSTR* icon) 
    { 
        std::wstring iconResourcePath = get_module_folderpath(g_hInst);
        iconResourcePath += L"\\data\\flutter_assets\\assets\\context_menu_icon";
        iconResourcePath += L"\\icon.ico";
        return SHStrDup(iconResourcePath.c_str(), icon);
    }
    IFACEMETHODIMP GetToolTip(_In_opt_ IShellItemArray*, _Outptr_result_nullonfailure_ PWSTR* infoTip) { *infoTip = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(_Out_ GUID* guidCommandName) { *guidCommandName = GUID_NULL;  return S_OK; }
    IFACEMETHODIMP GetState(_In_opt_ IShellItemArray* selection, _In_ BOOL okToBeSlow, _Out_ EXPCMDSTATE* cmdState)
    {
        if (nullptr == selection) {
            *cmdState = ECS_HIDDEN;
            return S_OK;
        }

        *cmdState = ECS_ENABLED;
        return S_OK;
    }
    IFACEMETHODIMP Invoke(_In_opt_ IShellItemArray* selection, _In_opt_ IBindCtx*) noexcept try
    {
        HWND parent = nullptr;
        if (m_site)
        {
            ComPtr<IOleWindow> oleWindow;
            RETURN_IF_FAILED(m_site.As(&oleWindow));
            RETURN_IF_FAILED(oleWindow->GetWindow(&parent));
        }

        std::wostringstream itemPaths;

        if (selection)
        {
            DWORD count = 0;
            selection->GetCount(&count);

            for (DWORD i = 0; i < count; i++)
            {
                IShellItem* shellItem;
                selection->GetItemAt(i, &shellItem);
                LPWSTR itemName;
                // Retrieves the entire file system path of the file from its shell item
                shellItem->GetDisplayName(SIGDN_FILESYSPATH, &itemName);
                CString fileName(itemName);
                // File name can't contain '?'
                // fileName.Append(_T("?"));
                // Write the file path into the input stream for image resizer
                itemPaths << L"\"" << std::wstring(fileName) << L"\"" << L" ";
            }
            //RETURN_IF_FAILED(selection->GetCount(&count));

            std::wstring executablePath = get_module_folderpath(g_hInst);
            executablePath += L"\\nocab_desktop.exe";
            ShellExecute(NULL, L"open", executablePath.c_str(), itemPaths.str().c_str(), get_module_folderpath(g_hInst).c_str(), SW_SHOWDEFAULT);
        }

        
        return S_OK;
    }
    CATCH_RETURN();

    IFACEMETHODIMP GetFlags(_Out_ EXPCMDFLAGS* flags) { *flags = Flags(); return S_OK; }
    IFACEMETHODIMP EnumSubCommands(_COM_Outptr_ IEnumExplorerCommand** enumCommands) { *enumCommands = nullptr; return E_NOTIMPL; }

    // IObjectWithSite
    IFACEMETHODIMP SetSite(_In_ IUnknown* site) noexcept { m_site = site; return S_OK; }
    IFACEMETHODIMP GetSite(_In_ REFIID riid, _COM_Outptr_ void** site) noexcept { return m_site.CopyTo(riid, site); }

protected:
    ComPtr<IUnknown> m_site;
};

CoCreatableClass(NoCabContextMenuCommand)
CoCreatableClassWrlCreatorMapInclude(NoCabContextMenuCommand)

STDAPI DllGetActivationFactory(_In_ HSTRING activatableClassId, _COM_Outptr_ IActivationFactory** factory)
{
    return Module<ModuleType::InProc>::GetModule().GetActivationFactory(activatableClassId, factory);
}

STDAPI DllCanUnloadNow()
{
    return Module<InProc>::GetModule().GetObjectCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID FAR* ppv)
{
    return Module<InProc>::GetModule().GetClassObject(rclsid, riid, ppv);
}


