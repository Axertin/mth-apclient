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
    wchar_t *end = path + n;
    const wchar_t tail[] = L"\\version.dll";
    for (size_t i = 0; i < (sizeof(tail) / sizeof(wchar_t)); ++i)
        *end++ = tail[i];
    g_real_version = LoadLibraryW(path);
}

} // namespace mth

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

// MSVC winver.h: input params are const; mingw-w64 winver.h: non-const. Match exactly.
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

// Win8+
PROXY_FWD(DWORD, GetFileVersionInfoByHandle, (LONG fl, HANDLE h, DWORD off, LPVOID d, DWORD len), (fl, h, off, d, len))
