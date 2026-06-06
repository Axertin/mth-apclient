// glibc_compat.h - force-included into the Dear ImGui build (Linux/x86_64).
//
// Why this exists:
//   We build on a modern host glibc, but the mod is LD_PRELOADed into the game,
//   which runs under the (older) Steam Linux Runtime glibc. glibc 2.41 re-versioned
//   several libm float functions, so building on a new host binds them to symbol
//   versions the runtime lacks:
//       sqrtf, acosf, atan2f  -> @GLIBC_2.43
//       fmodf                 -> @GLIBC_2.38
//       powf                  -> @GLIBC_2.27
//   A versioned symbol the runtime does not provide makes the dynamic loader reject
//   libmthap.so at load time, BEFORE our constructor runs, so the symptom is a
//   silent failure: the game window never opens and no log file is written.
//
//   Dear ImGui's anti-aliased geometry math is the only caller of these functions.
//   The old (@GLIBC_2.2.5) implementations are numerically equivalent for rendering,
//   so we pin every libm float symbol ImGui references to that ancient version,
//   keeping the .so loadable on old glibc. (scripts/check-glibc-max.sh enforces this
//   at build time.)
#pragma once

// NOTE: this is a -include (force-include) header, processed BEFORE any system
// header, so __GLIBC__ (from <features.h>) is not defined yet here. Guard only on
// the compiler-predefined target macros, which are always available. CMake only
// applies this include to non-Windows builds.
#if defined(__linux__) && defined(__x86_64__)
__asm__(".symver sqrtf,sqrtf@GLIBC_2.2.5");
__asm__(".symver acosf,acosf@GLIBC_2.2.5");
__asm__(".symver atan2f,atan2f@GLIBC_2.2.5");
__asm__(".symver fmodf,fmodf@GLIBC_2.2.5");
__asm__(".symver powf,powf@GLIBC_2.2.5");
__asm__(".symver cosf,cosf@GLIBC_2.2.5");
__asm__(".symver sinf,sinf@GLIBC_2.2.5");
__asm__(".symver logf,logf@GLIBC_2.2.5");
__asm__(".symver ceilf,ceilf@GLIBC_2.2.5");
__asm__(".symver floorf,floorf@GLIBC_2.2.5");
#endif
