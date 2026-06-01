#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pal
{

struct ModuleInfo
{
    std::uintptr_t base{0};
    std::size_t size{0};
    std::string path{};
};

// Main game executable module.
ModuleInfo game_module();

// Our injected DLL/SO.
ModuleInfo self_module();

// game_module().base + relative (convenience).
std::uintptr_t rva(std::uintptr_t relative);

// Linux: GNU BuildID; Windows: PE TimeDateStamp:SizeOfImage hex string.
std::string game_build_id();

// Look up an exported symbol in an already-loaded module.
//   Linux:   dlopen(module_basename, RTLD_NOLOAD) + dlsym; falls back to
//            dlsym(RTLD_DEFAULT) if the module isn't open by that name.
//   Windows: GetModuleHandleA(module_basename) + GetProcAddress.
// Returns nullptr if not found. `module_basename` may be nullptr to skip
// the per-module step and only consult the global symbol table.
void *find_symbol(const char *module_basename, const char *symbol);

} // namespace pal
