// The FreeSync rule (PushFreeSync.h): what the AMD driver's answers mean.
#include "veyra/gfx/PushFreeSync.h"
#include <cstdio>
using namespace veyra::gfx;
static int failures=0;
static void check(bool ok,const char* what){if(!ok){++failures;std::printf("FAIL: %s\n",what);}}
int main(){
    // caps 0xf: supported, GPU, display, current mode; use cases 7 = every one.
    const auto on=pushFreeSyncFrom(0,0xf,0,7);
    check(on.answered&&on.supported&&on.usableInMode&&on.switchedOn,"FreeSync on in the current mode");
    const auto off=pushFreeSyncFrom(0,0xf,0,0);
    check(off.supported&&off.usableInMode&&!off.switchedOn,"FreeSync switched off in AMD Software is not on");
    const auto mode=pushFreeSyncFrom(0,0x7,0,7);
    check(mode.supported&&!mode.usableInMode&&!mode.switchedOn,"a display mode without FreeSync is not on");
    const auto none=pushFreeSyncFrom(-1,0,-1,0);
    check(none.answered&&!none.supported&&!none.usableInMode&&!none.switchedOn,"a refused capability query is the answer: no FreeSync");
    const auto stateFailed=pushFreeSyncFrom(0,0xf,-1,7);
    check(stateFailed.supported&&!stateFailed.switchedOn,"a failed state query never says on");
    const auto garbage=pushFreeSyncFrom(0,0xf,0,7|(1<<30));
    check(garbage.switchedOn,"any use case counts");
    // Only the two display bits matter: GPU-only support is not the display's.
    const auto gpuOnly=pushFreeSyncFrom(0,0x3,0,7);
    check(!gpuOnly.supported&&!gpuOnly.switchedOn,"the graphics card's support alone is not the display's");
    static_assert(pushFreeSyncFrom(0,0xf,0,1).switchedOn,"the rule is constexpr");
    std::printf("%s\n",failures?"FAILED":"ok");
    return failures?1:0;
}
