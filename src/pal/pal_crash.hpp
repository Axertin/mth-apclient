#pragma once

namespace pal
{

// Install a process-wide crash handler. On a fatal fault it logs the faulting
// module+RVA and a stack backtrace to the pal log, then lets the default handler
// run. Windows (SEH/dbghelp) additionally writes a minidump to the log directory;
// Linux does not. Safe to call once, early.
void install_crash_handler();

} // namespace pal
