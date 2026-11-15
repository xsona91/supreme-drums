// Supreme Drums - minimal CoreWebView2EnvironmentOptions for MinGW/clang builds.
#pragma once
#include <wrl/implements.h>
#include "WebView2.h"
#include <objbase.h>
#include <string>
#define CORE_WEBVIEW_TARGET_PRODUCT_VERSION L"138.0.3351.48"
class CoreWebView2EnvironmentOptions : public Microsoft::WRL::Details::RefCounted<ICoreWebView2EnvironmentOptions> {
public:
    HRESULT STDMETHODCALLTYPE get_AdditionalBrowserArguments (LPWSTR* v) override { return out (args, v); }
    HRESULT STDMETHODCALLTYPE put_AdditionalBrowserArguments (LPCWSTR v) override { args = v ? v : L""; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Language (LPWSTR* v) override { return out (lang, v); }
    HRESULT STDMETHODCALLTYPE put_Language (LPCWSTR v) override { lang = v ? v : L""; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_TargetCompatibleBrowserVersion (LPWSTR* v) override { return out (target, v); }
    HRESULT STDMETHODCALLTYPE put_TargetCompatibleBrowserVersion (LPCWSTR v) override { target = v ? v : L""; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AllowSingleSignOnUsingOSPrimaryAccount (BOOL* v) override { if (! v) return E_POINTER; *v = sso; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_AllowSingleSignOnUsingOSPrimaryAccount (BOOL v) override { sso = v; return S_OK; }
private:
    static HRESULT out (const std::wstring& s, LPWSTR* v) {
        if (! v) return E_POINTER;
        const size_t bytes = (s.size() + 1) * sizeof (wchar_t);
        *v = static_cast<LPWSTR> (CoTaskMemAlloc (bytes));
        if (! *v) return E_OUTOFMEMORY;
        memcpy (*v, s.c_str(), bytes); return S_OK;
    }
    std::wstring args, lang, target { CORE_WEBVIEW_TARGET_PRODUCT_VERSION };
    BOOL sso = FALSE;
};
