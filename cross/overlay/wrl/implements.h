// Supreme Drums - minimal WRL replacement for MinGW/clang builds (MSVC uses the real WRL).
// Provides only what JUCE's WebView2 backend needs: Microsoft::WRL::Make and Callback.
#pragma once
#include <windows.h>
#include <unknwn.h>
#include <atomic>
#include <type_traits>
#include <utility>
namespace Microsoft { namespace WRL {
template <typename T> class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr (T* p, bool addRef) : ptr (p) { if (ptr && addRef) ptr->AddRef(); }
    ComPtr (const ComPtr& o) : ptr (o.ptr) { if (ptr) ptr->AddRef(); }
    ComPtr (ComPtr&& o) noexcept : ptr (o.ptr) { o.ptr = nullptr; }
    ~ComPtr() { if (ptr) ptr->Release(); }
    ComPtr& operator= (ComPtr o) noexcept { std::swap (ptr, o.ptr); return *this; }
    T* Get() const { return ptr; }
    T* operator->() const { return ptr; }
private:
    T* ptr = nullptr;
};
namespace Details {
template <typename Iface> struct RefCounted : Iface {
    std::atomic<ULONG> refs { 1 };
    virtual ~RefCounted() = default;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = --refs; if (r == 0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (IsEqualIID (riid, __uuidof (IUnknown)) || IsEqualIID (riid, __uuidof (Iface))) { *out = static_cast<Iface*> (this); AddRef(); return S_OK; }
        *out = nullptr; return E_NOINTERFACE;
    }
};
template <typename Iface, typename F, typename Sig> struct CallbackImpl;
template <typename Iface, typename F, typename C, typename... Args>
struct CallbackImpl<Iface, F, HRESULT (STDMETHODCALLTYPE C::*) (Args...)> : RefCounted<Iface> {
    explicit CallbackImpl (F f) : fn (std::move (f)) {}
    HRESULT STDMETHODCALLTYPE Invoke (Args... args) override { return fn (args...); }
    F fn;
};
} // namespace Details
template <typename T, typename... A> ComPtr<T> Make (A&&... a) { return ComPtr<T> (new T (std::forward<A> (a)...), false); }
template <typename Iface, typename F> ComPtr<Iface> Callback (F&& f) {
    using Impl = Details::CallbackImpl<Iface, std::decay_t<F>, decltype (&Iface::Invoke)>;
    return ComPtr<Iface> (static_cast<Iface*> (new Impl (std::forward<F> (f))), false);
}
}} // namespace Microsoft::WRL
