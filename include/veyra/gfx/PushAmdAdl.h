#pragma once
// Asks the AMD driver whether the display a window is on has FreeSync, and
// whether it is on (PushFreeSync.h judges the answer). Read only: nothing here
// changes a driver setting.
//
// The structure layouts and entry points are AMD's public Display Library
// interface (GPUOpen display-library, include/adl_structures.h and
// adl_defines.h, MIT); no SDK file is part of PushFPS. Loading follows the
// NVAPI query beside it: the System32 copy installed with the AMD driver only,
// pinned for the life of the process, one ADL context. On a PC without an AMD
// driver the file is not there and nothing is asked. VEYRA_NO_AMD_ADL=1 skips
// the query entirely (a way out if a driver ever misbehaves).
#include "veyra/gfx/PushFreeSync.h"
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
namespace veyra::gfx {
namespace adl {
// AdapterInfo (Windows layout, with iExist and the driver paths).
struct Adapter {
    int size,index;char udid[256];int bus,device,function,vendor;
    char adapterName[256],displayName[256];int present,exist;
    char driverPath[256],driverPathExt[256],pnp[256];int osDisplayIndex;
};
// ADLDisplayInfo: ADLDisplayID (4 ints), controller, two names, five ints.
struct Display {
    int logical,physical,logicalAdapter,physicalAdapter,controller;
    char name[256],manufacturer[256];int type,outputType,connector,mask,value;
};
// ADLFreeSyncCap.
struct Cap {int caps,minMicroHz,maxMicroHz;unsigned char label;char reserved[3];int reservedEx[4];};
static_assert(sizeof(Adapter)==1572,"ADL AdapterInfo layout");
static_assert(sizeof(Display)==552,"ADL ADLDisplayInfo layout");
static_assert(sizeof(Cap)==32,"ADL ADLFreeSyncCap layout");
// ADL_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED | ADL_DISPLAY_DISPLAYINFO_DISPLAYMAPPED.
inline constexpr int kConnectedMapped=0x3;
// Bounds on what ADL hands back, so a confused answer is never walked past.
inline constexpr int kMaxAdapters=64,kMaxDisplays=64;
struct Library {
    using Alloc=void*(__stdcall*)(int);
    using Create=int(__cdecl*)(Alloc,int,void**);
    using Count=int(__cdecl*)(void*,int*);
    using Adapters=int(__cdecl*)(void*,Adapter*,int);
    using Displays=int(__cdecl*)(void*,int,int*,Display**,int);
    using GetCap=int(__cdecl*)(void*,int,int,Cap*);
    using GetState=int(__cdecl*)(void*,int,int,int*,int*,int*,int*);
    void* context=nullptr;Count count=nullptr;Adapters adapters=nullptr;Displays displays=nullptr;
    GetCap getCap=nullptr;GetState getState=nullptr;
    bool ready()const{return context&&count&&adapters&&displays&&getCap&&getState;}
};
inline void* __stdcall allocate(int size){return size>0?std::malloc(size_t(size)):nullptr;}
inline const Library& library(){
    static Library lib;static std::once_flag once;
    std::call_once(once,[]{
        wchar_t skip[4]{};
        if(GetEnvironmentVariableW(L"VEYRA_NO_AMD_ADL",skip,4)&&skip[0]==L'1')return;
        wchar_t system[MAX_PATH]{};
        const UINT n=GetSystemDirectoryW(system,MAX_PATH);
        if(n==0||n>=MAX_PATH)return;
        const std::wstring path=std::wstring(system)+L"\\atiadlxx.dll";
        HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)return;
        HMODULE pinned=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(module),&pinned);
        auto create=reinterpret_cast<Library::Create>(GetProcAddress(module,"ADL2_Main_Control_Create"));
        Library l;
        l.count=reinterpret_cast<Library::Count>(GetProcAddress(module,"ADL2_Adapter_NumberOfAdapters_Get"));
        l.adapters=reinterpret_cast<Library::Adapters>(GetProcAddress(module,"ADL2_Adapter_AdapterInfo_Get"));
        l.displays=reinterpret_cast<Library::Displays>(GetProcAddress(module,"ADL2_Display_DisplayInfo_Get"));
        l.getCap=reinterpret_cast<Library::GetCap>(GetProcAddress(module,"ADL2_Display_FreeSync_Cap"));
        l.getState=reinterpret_cast<Library::GetState>(GetProcAddress(module,"ADL2_Display_FreeSyncState_Get"));
        if(!create||!l.count||!l.adapters||!l.displays||!l.getCap||!l.getState)return;
        void* context=nullptr;
        // 1 = enumerate connected adapters only.
        if(create(&allocate,1,&context)!=0||!context)return;
        l.context=context;lib=l;
    });
    return lib;
}
}
// The display's FreeSync, or nothing when the AMD driver does not know the
// GDI device (another vendor's display, no AMD driver, the query skipped).
// gdiDevice: MONITORINFOEXW::szDevice, e.g. \\.\DISPLAY1.
inline std::optional<PushFreeSync> queryAmdFreeSync(const wchar_t* gdiDevice){
    const adl::Library& lib=adl::library();
    if(!lib.ready()||!gdiDevice||!gdiDevice[0])return std::nullopt;
    char device[CCHDEVICENAME*2]{};
    if(!WideCharToMultiByte(CP_ACP,0,gdiDevice,-1,device,int(sizeof(device)),nullptr,nullptr))return std::nullopt;
    int adapterCount=0;
    if(lib.count(lib.context,&adapterCount)!=0||adapterCount<=0||adapterCount>adl::kMaxAdapters)return std::nullopt;
    std::vector<adl::Adapter> list(static_cast<size_t>(adapterCount));
    for(auto& a:list){std::memset(&a,0,sizeof(a));a.size=int(sizeof(adl::Adapter));}
    if(lib.adapters(lib.context,list.data(),int(sizeof(adl::Adapter))*adapterCount)!=0)return std::nullopt;
    for(auto& a:list){
        a.displayName[sizeof(a.displayName)-1]=0;
        if(std::strcmp(a.displayName,device)!=0)continue;
        int n=0;adl::Display* info=nullptr;
        const int listed=lib.displays(lib.context,a.index,&n,&info,0);
        int display=-1;
        if(listed==0&&info&&n>0&&n<=adl::kMaxDisplays){
            for(int i=0;i<n;++i){
                if(info[i].logicalAdapter==a.index&&(info[i].value&adl::kConnectedMapped)==adl::kConnectedMapped){display=info[i].logical;break;}
            }
        }
        std::free(info);
        if(display<0)continue;
        adl::Cap cap{};int useCases=0,defaults=0,minMicroHz=0,maxMicroHz=0;
        const int capStatus=lib.getCap(lib.context,a.index,display,&cap);
        const int stateStatus=capStatus==0?lib.getState(lib.context,a.index,display,&useCases,&defaults,&minMicroHz,&maxMicroHz):capStatus;
        return pushFreeSyncFrom(capStatus,cap.caps,stateStatus,useCases);
    }
    return std::nullopt;
}
}
