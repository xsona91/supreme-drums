/*
  Supreme Drums
  Copyright (c) 2026 Giulia Casu. All rights reserved.
  Small shims needed only by the MinGW/clang cross-build (never compiled with MSVC).
*/
#include <windows.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* SAPI text-to-speech voice class id (used by JUCE's accessibility code). */
const CLSID CLSID_SpVoice = { 0x96749377, 0x3391, 0x11D2, { 0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96 } };

/* _get_tzname lives in the UCRT; provide it on top of msvcrt's _tzname. */
static int __cdecl sd_get_tzname (size_t* ret, char* buf, size_t size, int index)
{
    if (ret == NULL || index < 0 || index > 1) return EINVAL;
    const char* s = _tzname[index] ? _tzname[index] : "";
    const size_t n = strlen (s) + 1;
    *ret = n;
    if (buf == NULL) return 0;
    if (size < n) return ERANGE;
    memcpy (buf, s, n);
    return 0;
}
void* __imp__get_tzname = (void*) sd_get_tzname;

/* TaskDialogIndirect exists only in comctl32 v6 (side-by-side). A plugin DLL
   has no activation context of its own, so importing it statically makes
   Windows refuse to load the plugin. Resolve it at run time instead and fall
   back to a plain MessageBox when it is unavailable. */
#include <commctrl.h>
typedef HRESULT (WINAPI* SdTaskDialogFn) (const TASKDIALOGCONFIG*, int*, int*, BOOL*);
static HRESULT WINAPI sd_TaskDialogIndirect (const TASKDIALOGCONFIG* c, int* button, int* radio, BOOL* verify)
{
    HMODULE m = GetModuleHandleW (L"comctl32.dll");
    if (m == NULL) m = LoadLibraryW (L"comctl32.dll");
    SdTaskDialogFn fn = m ? (SdTaskDialogFn) (void*) GetProcAddress (m, "TaskDialogIndirect") : NULL;
    if (fn != NULL) return fn (c, button, radio, verify);
    const wchar_t* text  = (c && c->pszContent && ! IS_INTRESOURCE (c->pszContent)) ? c->pszContent : L"";
    const wchar_t* title = (c && c->pszWindowTitle && ! IS_INTRESOURCE (c->pszWindowTitle)) ? c->pszWindowTitle : L"Supreme Drums";
    int r = MessageBoxW (c ? c->hwndParent : NULL, text, title, (c && c->cButtons > 1) ? MB_OKCANCEL : MB_OK);
    if (button) *button = (c && c->cButtons > 0 && c->pButtons) ? (r == IDOK ? c->pButtons[0].nButtonID : c->pButtons[c->cButtons - 1].nButtonID) : r;
    if (radio) *radio = 0;
    if (verify) *verify = FALSE;
    return S_OK;
}
void* __imp_TaskDialogIndirect = (void*) sd_TaskDialogIndirect;
