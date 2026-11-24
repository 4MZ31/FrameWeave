#pragma once
// AMD FreeSync of one display, judged from what the AMD driver's display
// library (ADL) answers. Pure: no Windows header, so the rule is tested
// anywhere; the query that feeds it is PushAmdAdl.h.
//
// What ADL can and cannot say. ADL2_Display_FreeSync_Cap gives the display's
// capability bits; ADL2_Display_FreeSyncState_Get gives the use cases FreeSync
// is switched on for in AMD Software (0 = switched off). Neither says whether
// a flip is shown in variable refresh at this moment, so the best this answer
// reaches is "may be requested" (level 3 in NvidiaVrr.h) and never "active".
// A display without FreeSync makes the driver refuse the capability query: that
// refusal is the answer "no FreeSync", not a failed query to be retried.
namespace veyra::gfx {
struct PushFreeSync {
    bool answered=false;         // the AMD driver knows this display
    bool supported=false;        // the display can do FreeSync
    bool usableInMode=false;     // ... with the display mode now applied
    bool switchedOn=false;       // FreeSync is on in AMD Software for it
    bool operator==(const PushFreeSync&)const=default;
};
// ADL_FREESYNC_CAP_DISPLAYSUPPORTED / ADL_FREESYNC_CAP_CURRENTMODESUPPORTED.
inline constexpr int kPushFreeSyncDisplay=1<<2;
inline constexpr int kPushFreeSyncCurrentMode=1<<3;
// capStatus / stateStatus: ADL's return codes (0 = ADL_OK); caps: the
// capability bits; useCases: the current state's use-case bits.
constexpr PushFreeSync pushFreeSyncFrom(int capStatus,int caps,int stateStatus,int useCases){
    PushFreeSync s;s.answered=true;
    if(capStatus!=0)return s;
    s.supported=(caps&kPushFreeSyncDisplay)!=0;
    s.usableInMode=(caps&kPushFreeSyncCurrentMode)!=0;
    s.switchedOn=s.supported&&s.usableInMode&&stateStatus==0&&useCases!=0;
    return s;
}
}
