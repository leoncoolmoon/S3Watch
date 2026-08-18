// Single translation unit that compiles the public-domain minimp3 decoder
// (https://github.com/lieff/minimp3, vendored verbatim in this directory).
// minimp3.h/minimp3_ex.h are header-only libraries whose implementations are
// gated behind MINIMP3_IMPLEMENTATION — define it exactly once, here.
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
