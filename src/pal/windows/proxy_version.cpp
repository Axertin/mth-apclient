// Runtime proxy for version.dll. The mod ships as `version.dll` next to
// MinaTheHollower.exe, which statically imports VERSION.DLL - so the loader
// maps us in its place. DllMain (entry_windows.cpp) loads the real version.dll
// from System32 and bootstraps the mod; this file forwards the standard
// exports to the real DLL.
//
// x86_64 note: unlike the i686 stdcall ABI, x64 applies no name decoration, so
// __declspec(dllexport) on an extern "C" function publishes the undecorated
// export name directly (e.g. "GetFileVersionInfoW") - no /EXPORT alias needed.
// Lookups via GetProcAddress are lazy, per-function on first use.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <winver.h>

namespace
{

HMODULE g_real_version = nullptr;

} // namespace

namespace mth
{

void proxy_version_load_real()
{
    if (g_real_version)
        return;
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 16)
        return;
    // Append "\\version.dll" to the System32 path.
    wchar_t *end = path + n;
    const wchar_t tail[] = L"\\version.dll";
    for (size_t i = 0; i < (sizeof(tail) / sizeof(wchar_t)); ++i)
        *end++ = tail[i];
    g_real_version = LoadLibraryW(path);
}

} // namespace mth

// Declares an export that lazily resolves the real function on first call and
// forwards. Returns a default-constructed Ret if the real DLL is unavailable.
#define PROXY_FWD(Ret, Name, Params, Args)                                                                                                                     \
    extern "C" __declspec(dllexport) Ret WINAPI Name Params                                                                                                    \
    {                                                                                                                                                          \
        using Fn = Ret(WINAPI *) Params;                                                                                                                       \
        static Fn fn = reinterpret_cast<Fn>(g_real_version ? GetProcAddress(g_real_version, #Name) : nullptr);                                                 \
        return fn ? fn Args : Ret{};                                                                                                                           \
    }

PROXY_FWD(BOOL, GetFileVersionInfoA, (LPCSTR f, DWORD h, DWORD l, LPVOID d), (f, h, l, d))

PROXY_FWD(BOOL, GetFileVersionInfoW, (LPCWSTR f, DWORD h, DWORD l, LPVOID d), (f, h, l, d))

PROXY_FWD(BOOL, GetFileVersionInfoExA, (DWORD fl, LPCSTR f, DWORD h, DWORD l, LPVOID d), (fl, f, h, l, d))

PROXY_FWD(BOOL, GetFileVersionInfoExW, (DWORD fl, LPCWSTR f, DWORD h, DWORD l, LPVOID d), (fl, f, h, l, d))

PROXY_FWD(DWORD, GetFileVersionInfoSizeA, (LPCSTR f, LPDWORD h), (f, h))

PROXY_FWD(DWORD, GetFileVersionInfoSizeW, (LPCWSTR f, LPDWORD h), (f, h))

PROXY_FWD(DWORD, GetFileVersionInfoSizeExA, (DWORD fl, LPCSTR f, LPDWORD h), (fl, f, h))

PROXY_FWD(DWORD, GetFileVersionInfoSizeExW, (DWORD fl, LPCWSTR f, LPDWORD h), (fl, f, h))

PROXY_FWD(DWORD, VerLanguageNameA, (DWORD lang, LPSTR buf, DWORD cch), (lang, buf, cch))

PROXY_FWD(DWORD, VerLanguageNameW, (DWORD lang, LPWSTR buf, DWORD cch), (lang, buf, cch))

PROXY_FWD(BOOL, VerQueryValueA, (LPCVOID blk, LPCSTR sub, LPVOID *out, PUINT len), (blk, sub, out, len))

PROXY_FWD(BOOL, VerQueryValueW, (LPCVOID blk, LPCWSTR sub, LPVOID *out, PUINT len), (blk, sub, out, len))

// VerFindFile/VerInstallFile: MSVC's winver.h marks input-direction params
// const (LPCSTR/LPCWSTR); mingw-w64's winver.h declares them all non-const
// (LPSTR/LPWSTR). Match each header exactly to avoid conflicting-types errors
// on redeclaration. clang-cl defines _MSC_VER; the LLVM-MinGW dev cross does not.
#ifdef _MSC_VER
PROXY_FWD(DWORD, VerFindFileA, (DWORD fl, LPCSTR f, LPCSTR wd, LPCSTR ad, LPSTR cd, PUINT cdl, LPSTR dd, PUINT ddl), (fl, f, wd, ad, cd, cdl, dd, ddl))

PROXY_FWD(DWORD, VerFindFileW, (DWORD fl, LPCWSTR f, LPCWSTR wd, LPCWSTR ad, LPWSTR cd, PUINT cdl, LPWSTR dd, PUINT ddl), (fl, f, wd, ad, cd, cdl, dd, ddl))

PROXY_FWD(DWORD, VerInstallFileA, (DWORD fl, LPCSTR s, LPCSTR d, LPCSTR sd, LPCSTR dd, LPCSTR cd, LPSTR tmp, PUINT tmpl), (fl, s, d, sd, dd, cd, tmp, tmpl))

PROXY_FWD(DWORD, VerInstallFileW, (DWORD fl, LPCWSTR s, LPCWSTR d, LPCWSTR sd, LPCWSTR dd, LPCWSTR cd, LPWSTR tmp, PUINT tmpl),
          (fl, s, d, sd, dd, cd, tmp, tmpl))
#else
PROXY_FWD(DWORD, VerFindFileA, (DWORD fl, LPSTR f, LPSTR wd, LPSTR ad, LPSTR cd, PUINT cdl, LPSTR dd, PUINT ddl), (fl, f, wd, ad, cd, cdl, dd, ddl))

PROXY_FWD(DWORD, VerFindFileW, (DWORD fl, LPWSTR f, LPWSTR wd, LPWSTR ad, LPWSTR cd, PUINT cdl, LPWSTR dd, PUINT ddl), (fl, f, wd, ad, cd, cdl, dd, ddl))

PROXY_FWD(DWORD, VerInstallFileA, (DWORD fl, LPSTR s, LPSTR d, LPSTR sd, LPSTR dd, LPSTR cd, LPSTR tmp, PUINT tmpl), (fl, s, d, sd, dd, cd, tmp, tmpl))

PROXY_FWD(DWORD, VerInstallFileW, (DWORD fl, LPWSTR s, LPWSTR d, LPWSTR sd, LPWSTR dd, LPWSTR cd, LPWSTR tmp, PUINT tmpl), (fl, s, d, sd, dd, cd, tmp, tmpl))
#endif

// Win8+ export; included for drop-in parity with a modern version.dll.
PROXY_FWD(DWORD, GetFileVersionInfoByHandle, (LONG fl, HANDLE h, DWORD off, LPVOID d, DWORD len), (fl, h, off, d, len))
