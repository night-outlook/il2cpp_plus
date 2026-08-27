#pragma once

// Experimental Assembly Shadow is opt-in at the native compiler boundary.
// Do not derive this value from a managed scripting define.
#ifndef HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#define HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW 0
#endif

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW != 0 && HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW != 1
#error HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW must be 0 or 1
#endif
