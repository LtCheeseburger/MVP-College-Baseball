// Read-only acceptance checks for the two native listed-prospect containers.
#include "core/dev_tools.h"
#include "core/log.h"
#include "game/phase9.h"
#include "kernel/kernel.h"
#include <stdexcept>

namespace {
void Verify(uint32_t self,uint32_t caller,uint32_t expectedCaller,const char* label) {
    if(caller!=expectedCaller || !dev::Env("MVP06_TEST_RECRUITING"))return;
    const auto expected=phase9::Enabled(phase9::Feature::ModernDynasty)?phase9::kRecruitProspects:400;
    const auto actual=kernel::R32(self+8);
    if(actual!=expected)throw std::runtime_error("Recruiting: native listed-prospect capacity differs from the active policy");
    LOGR("recruiting acceptance: %s native capacity %u",label,actual);
}
}
X86_EXTERN_FUNC(__imp__sub_004DCA80);
X86_FUNC(sub_004DCA80) {
    const auto self=ctx.ecx,caller=kernel::R32(ctx.esp);
    __imp__sub_004DCA80(ctx,base);
    Verify(self,caller,0x98C69,"first container");
}
X86_EXTERN_FUNC(__imp__sub_004DCB30);
X86_FUNC(sub_004DCB30) {
    const auto self=ctx.ecx,caller=kernel::R32(ctx.esp);
    __imp__sub_004DCB30(ctx,base);
    Verify(self,caller,0x98CA3,"second container");
}
X86_EXTERN_FUNC(__imp__sub_004FCE80);
X86_FUNC(sub_004FCE80) {
    if(!dev::Env("MVP06_TEST_RECRUITING"))return __imp__sub_004FCE80(ctx,base);
    const auto database=kernel::R32(ctx.esp+4);
    const auto entry=ctx;ctx.ecx=database;
    const auto before=kernel::CallGuest(ctx,0x785A0,{});ctx=entry;
    __imp__sub_004FCE80(ctx,base);
    const auto saved=ctx;ctx.ecx=database;
    const auto count=kernel::CallGuest(ctx,0x785A0,{});ctx=saved;
    const auto expected=phase9::Enabled(phase9::Feature::ModernDynasty)?phase9::kRecruitProspects:400;
    if(count!=(before<expected?expected:before))throw std::runtime_error("Recruiting: native prospect fill differs from policy");
    LOGR("recruiting acceptance: native prospect fill %u -> %u (listed minimum %u)",before,count,expected);
}
