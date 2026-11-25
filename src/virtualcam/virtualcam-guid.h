// PushFPS's own device identity: distinct from OBS's camera and from Veyra's, so all three can be installed
// side by side and none ever replaces or removes another. Do not change across updates.
#pragma once
#include <guiddef.h>
inline constexpr GUID CLSID_Veyra_VirtualVideo={0x71e63d77,0x9e79,0x4521,{0x86,0xfe,0x02,0x82,0x8b,0x76,0x82,0xca}};
inline constexpr wchar_t VeyraCameraClsid[]=L"{71E63D77-9E79-4521-86FE-02828B7682CA}";
inline constexpr wchar_t VeyraCameraName[]=L"FrameWeave Virtual Camera";
