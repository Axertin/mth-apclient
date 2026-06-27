// MinGW CRT shim (llvm-mingw build only; never compiled for clang-cl/MSVC).
//
// vcpkg's OpenSSL (built for the msvcrt CRT) references _vsnprintf through
// __declspec(dllimport), i.e. it needs the import pointer __imp__vsnprintf.
// llvm-mingw's libmsvcrt.a supplies _vsnprintf only as a plain static symbol,
// not an import thunk, so a fully-static (-static) link - which we want for a
// self-contained mod.dll - cannot satisfy the dllimport reference. Supply
// the missing import pointer ourselves, aimed at the static CRT function. This
// is link-only glue (_vsnprintf is pure formatting, so no cross-CRT state
// crosses it), gated to MINGW in CMake so the MSVC build is untouched.
//
// size_t / va_list are spelled as compiler builtins to avoid pulling a CRT
// header into this tiny TU.

extern "C" int _vsnprintf(char *, decltype(sizeof(0)), const char *, __builtin_va_list);
extern "C" void *__imp__vsnprintf = reinterpret_cast<void *>(&_vsnprintf);
