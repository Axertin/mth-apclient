// Windows crash handler: an unhandled-exception filter that logs the faulting
// module+RVA and a symbolized stack backtrace into the pal log, and writes a
// minidump to the log directory for offline analysis (WinDbg / Visual Studio).
//
// The module+RVA frames are the key diagnostic for this stripped game: subtract
// nothing - the RVA is already relative to each module's base, so a frame in
// MinaTheHollower.exe maps straight to a Ghidra address, and a frame in
// version.dll maps to our own code (symbolized directly if its PDB is alongside).

#include "pal/pal_crash.hpp"
#include "pal/pal_log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <dbghelp.h>

namespace pal
{

namespace
{

std::wstring g_dump_dir;        // captured at install time (avoid alloc at crash time)
volatile LONG g_in_handler = 0; // re-entrancy guard

const char *basename_of(const char *path)
{
    const char *slash = std::strrchr(path, '\\');
    return slash ? slash + 1 : path;
}

// Resolve an address to "module.dll+0xRVA"; returns the module base (0 if unknown).
std::uintptr_t module_for(void *addr, char *out, std::size_t out_sz)
{
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(addr), &mod))
    {
        std::snprintf(out, out_sz, "(unknown)");
        return 0;
    }
    char modpath[MAX_PATH] = {0};
    GetModuleFileNameA(mod, modpath, MAX_PATH);
    std::snprintf(out, out_sz, "%s", basename_of(modpath));
    return reinterpret_cast<std::uintptr_t>(mod);
}

void write_minidump(EXCEPTION_POINTERS *ep)
{
    if (g_dump_dir.empty())
        return;
    wchar_t path[1024];
    std::swprintf(path, 1024, L"%ls\\crash_%lu.dmp", g_dump_dir.c_str(), GetCurrentProcessId());
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
    {
        pal::logf(pal::LogLevel::Error, "crash: could not create minidump file (err=%lu)", GetLastError());
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory);
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, type, &mei, nullptr, nullptr);
    CloseHandle(f);
    pal::logf(pal::LogLevel::Error, "crash: minidump %s -> crash_%lu.dmp (in log dir)", ok ? "written" : "FAILED", GetCurrentProcessId());
}

// Walk the faulting thread's stack from the exception context (NOT the filter's
// own stack), resolving each frame to module+RVA and, where symbols exist, name.
void log_backtrace(CONTEXT *src_ctx)
{
    const HANDLE proc = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();

    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
    SymInitialize(proc, nullptr, TRUE);

    CONTEXT ctx = *src_ctx; // StackWalk64 mutates the context
    STACKFRAME64 frame{};
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;

    for (int i = 0; i < 64; ++i)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &frame, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        const DWORD64 pc = frame.AddrPC.Offset;
        if (pc == 0)
            break;

        char modname[64] = {0};
        const std::uintptr_t base = module_for(reinterpret_cast<void *>(pc), modname, sizeof(modname));
        const unsigned long long rva = base ? static_cast<unsigned long long>(pc) - base : 0;

        char symbuf[sizeof(SYMBOL_INFO) + 256] = {0};
        auto *sym = reinterpret_cast<SYMBOL_INFO *>(symbuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        if (SymFromAddr(proc, pc, &disp, sym))
            pal::logf(pal::LogLevel::Error, "crash: #%02d %s+0x%llx  %s+0x%llx", i, modname, rva, sym->Name, static_cast<unsigned long long>(disp));
        else
            pal::logf(pal::LogLevel::Error, "crash: #%02d %s+0x%llx  (0x%llx)", i, modname, rva, static_cast<unsigned long long>(pc));
    }

    SymCleanup(proc);
}

// Exception codes that are essentially always fatal (not used for control flow),
// so the vectored handler can act on them without logging benign first-chance
// exceptions (e.g. C++ EH throws, debugger notifications).
bool is_fatal_code(DWORD c)
{
    switch (c)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case 0xC0000409: // STATUS_STACK_BUFFER_OVERRUN (fast-fail; may not reach us)
    case 0xC0000374: // STATUS_HEAP_CORRUPTION (fast-fail; may not reach us)
        return true;
    default:
        return false;
    }
}

void report_crash(EXCEPTION_POINTERS *ep, const char *source)
{
    if (InterlockedExchange(&g_in_handler, 1) != 0)
        return; // already inside the handler (recursion / the other entry point)

    const EXCEPTION_RECORD *er = ep ? ep->ExceptionRecord : nullptr;
    const unsigned long code = er ? er->ExceptionCode : 0;
    void *addr = er ? er->ExceptionAddress : nullptr;

    char modname[64] = {0};
    const std::uintptr_t base = module_for(addr, modname, sizeof(modname));
    const unsigned long long rva = base ? reinterpret_cast<std::uintptr_t>(addr) - base : 0;
    pal::logf(pal::LogLevel::Error, "crash: ==== fatal exception (%s) ====", source);
    pal::logf(pal::LogLevel::Error, "crash: code=0x%08lx at %s+0x%llx (%p)", code, modname, rva, addr);
    if (code == EXCEPTION_ACCESS_VIOLATION && er && er->NumberParameters >= 2)
    {
        const ULONG_PTR rw = er->ExceptionInformation[0];
        const char *op = rw == 0 ? "read" : (rw == 1 ? "write" : "execute");
        pal::logf(pal::LogLevel::Error, "crash: access violation (%s) of 0x%p", op, reinterpret_cast<void *>(er->ExceptionInformation[1]));
    }

    write_minidump(ep); // overwrites crash_<pid>.dmp; the final crash leaves the relevant dump
    if (ep && ep->ContextRecord)
        log_backtrace(ep->ContextRecord);

    pal::default_log().flush(); // ensure it reaches disk before the process dies
    InterlockedExchange(&g_in_handler, 0);
}

// Unhandled-exception filter: fires only for truly-unhandled exceptions, but the
// game may replace it (SetUnhandledExceptionFilter is last-writer-wins).
LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    report_crash(ep, "unhandled-filter");
    return EXCEPTION_CONTINUE_SEARCH; // let the default handler terminate normally
}

// Vectored handler: cannot be overridden by SetUnhandledExceptionFilter and runs
// before the SEH chain, so it catches crashes even if the game replaced our
// filter. Gated to fatal codes to avoid acting on benign first-chance exceptions.
LONG CALLBACK veh_handler(EXCEPTION_POINTERS *ep)
{
    if (ep && ep->ExceptionRecord && is_fatal_code(ep->ExceptionRecord->ExceptionCode))
        report_crash(ep, "vectored");
    return EXCEPTION_CONTINUE_SEARCH; // never swallow; let normal handling proceed
}

} // namespace

void install_crash_handler()
{
    try
    {
        g_dump_dir = pal::log_dir().wstring();
    }
    catch (...)
    {
        // leave g_dump_dir empty; backtrace still logs, just no minidump file
    }
    AddVectoredExceptionHandler(1, &veh_handler); // 1 = call first
    SetUnhandledExceptionFilter(&crash_filter);
    pal::logf(pal::LogLevel::Info, "crash: handlers installed (vectored + unhandled-filter; dumps -> log dir)");
}

} // namespace pal
