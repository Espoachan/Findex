#include "shell.hpp"

#include <windows.h>
#include <exdisp.h>
#include <shellapi.h>
#include <shldisp.h>
#include <shlguid.h>
#include <shlobj.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

// Asks the desktop's Explorer instance to run the command, so the new process
// inherits Explorer's (unelevated) token instead of ours.
// See Raymond Chen, "How can I launch an unelevated process from my elevated process?"
bool shellExecuteFromExplorer(const std::wstring& file, const std::wstring& params) {
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows))))
        return false;

    VARIANT loc;
    VariantInit(&loc);
    loc.vt = VT_I4;
    loc.lVal = CSIDL_DESKTOP;
    VARIANT empty;
    VariantInit(&empty);
    long hwnd = 0;
    ComPtr<IDispatch> disp;
    if (windows->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &disp) != S_OK || !disp)
        return false;

    ComPtr<IServiceProvider> provider;
    ComPtr<IShellBrowser> browser;
    ComPtr<IShellView> view;
    ComPtr<IDispatch> view_disp;
    ComPtr<IShellFolderViewDual> folder_view;
    ComPtr<IDispatch> app_disp;
    ComPtr<IShellDispatch2> shell;
    if (FAILED(disp.As(&provider)) ||
        FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) ||
        FAILED(browser->QueryActiveShellView(&view)) ||
        FAILED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&view_disp))) ||
        FAILED(view_disp.As(&folder_view)) || FAILED(folder_view->get_Application(&app_disp)) ||
        FAILED(app_disp.As(&shell)))
        return false;

    BSTR b_file = SysAllocString(file.c_str());
    VARIANT v_params, v_dir, v_op, v_show;
    VariantInit(&v_params);
    VariantInit(&v_dir);
    VariantInit(&v_op);
    VariantInit(&v_show);
    v_params.vt = VT_BSTR;
    v_params.bstrVal = SysAllocString(params.c_str());
    v_dir.vt = VT_BSTR;
    v_dir.bstrVal = SysAllocString(L"");
    v_op.vt = VT_BSTR;
    v_op.bstrVal = SysAllocString(L"");
    v_show.vt = VT_I4;
    v_show.lVal = SW_SHOWNORMAL;

    HRESULT hr = shell->ShellExecute(b_file, v_params, v_dir, v_op, v_show);

    SysFreeString(b_file);
    VariantClear(&v_params);
    VariantClear(&v_dir);
    VariantClear(&v_op);
    return SUCCEEDED(hr);
}

bool run(const std::wstring& file, const std::wstring& params) {
    if (shellExecuteFromExplorer(file, params)) return true;
    auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(),
                                                     params.empty() ? nullptr : params.c_str(), nullptr,
                                                     SW_SHOWNORMAL));
    return r > 32;
}

} // namespace

namespace shell {

bool open(const std::wstring& path) { return run(path, L""); }

bool reveal(const std::wstring& path) { return run(L"explorer.exe", L"/select,\"" + path + L"\""); }

bool copyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem) {
        memcpy(GlobalLock(mem), text.c_str(), bytes);
        GlobalUnlock(mem);
        ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

} // namespace shell
