// Walk-up songs: the Ogg Vorbis decoder walkup.cpp uses, in its own translation unit (stb_vorbis defines
// many short macros). third_party/stb_vorbis.c is Sean Barrett's stb_vorbis v1.22, public domain or MIT (end of file).
#define STB_VORBIS_NO_PUSHDATA_API
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include "third_party/stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
