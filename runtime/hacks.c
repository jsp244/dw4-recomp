/*
 * hacks.c - the ROM hacks that come with the program (hacks.h).
 */
#include "hacks.h"
#include "patch.h"

#if defined(__has_include)
#  if __has_include("../generated/hacks.h")
#    include "../generated/hacks.h"
#    define HAVE_HACKS 1
#  endif
#endif

int hacks_count(void) {
#ifdef HAVE_HACKS
    int n = (int)(sizeof builtin_hacks / sizeof builtin_hacks[0]);
    return n > HACKS_MAX ? HACKS_MAX : n;
#else
    return 0;
#endif
}

const hack_t *hacks_get(int i) {
#ifdef HAVE_HACKS
    return i >= 0 && i < hacks_count() ? &builtin_hacks[i] : 0;
#else
    (void)i;
    return 0;
#endif
}
