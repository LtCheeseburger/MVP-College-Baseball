// AMD FidelityFX Super Resolution 1 (MIT, ffx/LICENSE-FidelityFX-FSR.txt): its two headers as the shader compiler
// takes them, verbatim inside raw strings (ffx/VERSION.txt names the commit)
namespace gpu {

extern const char* const kFfxA =
#include "ffx/ffx_a.h.inc"
    ;

extern const char* const kFfxFsr1 =
#include "ffx/ffx_fsr1.h.inc"
    ;

}  // namespace gpu
