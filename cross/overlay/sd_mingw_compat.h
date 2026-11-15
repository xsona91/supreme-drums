// Supreme Drums - MinGW/clang build helpers (not used by MSVC builds).
#pragma once
#ifdef __cplusplus
#include <_mingw.h>
// Macros that the newer Windows API headers expect from a newer mingw-w64 CRT.
#ifndef __MINGW_CXX11_CONSTEXPR
 #define __MINGW_CXX11_CONSTEXPR constexpr
#endif
#ifndef __MINGW_CXX14_CONSTEXPR
 #define __MINGW_CXX14_CONSTEXPR constexpr
#endif
#ifndef __MINGW_PRAGMA_PARAM_IMPL
 #define __MINGW_PRAGMA_PARAM_IMPL(x) _Pragma (#x)
#endif
#ifndef __MINGW_USE___UUIDOF
 #define __MINGW_USE___UUIDOF 0
#endif
#include <guiddef.h>
#include <cstring>
#include <cstdlib>
#include <cwchar>
// MinGW maps __uuidof to __mingw_uuidof<T>(), which only knows interfaces declared with
// __CRT_UUID_DECL. Give it a fallback that uses clang's real __uuidof (interfaces declared
// with __declspec(uuid), e.g. the official WebView2 headers).
#pragma push_macro("__uuidof")
#undef __uuidof
namespace juce { template <class ComClass> class ComSmartPtr; }
template <typename T> struct sd_uuid { static constexpr const GUID& get() { return __uuidof (T); } };
template <typename T> constexpr const GUID& __mingw_uuidof();
template <typename C> struct sd_uuid<juce::ComSmartPtr<C>> { static constexpr const GUID& get() { return __mingw_uuidof<C>(); } };
template <typename T> constexpr const GUID& __mingw_uuidof() { return sd_uuid<T>::get(); }
#pragma pop_macro("__uuidof")
// UI Automation caret positions (Windows 10 SDK) missing from mingw-w64 headers.
enum CaretPosition { CaretPosition_Unknown = 0, CaretPosition_EndOfLine = 1, CaretPosition_BeginningOfLine = 2 };
#endif
