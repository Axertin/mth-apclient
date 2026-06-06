#pragma once

namespace pal
{

// Install a process-wide crash handler. On a fatal fault it logs the faulting
// module+RVA and a stack backtrace to the pal log, and writes a minidump to the
// log directory, then lets the default handler run. Platform-specific backend
// (Windows: SEH/dbghelp). Safe to call once, early.
void install_crash_handler();

} // namespace pal
