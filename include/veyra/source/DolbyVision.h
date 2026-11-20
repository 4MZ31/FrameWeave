#pragma once
#include "veyra/pipeline/FramePacket.h"
#include <cstdint>
#include <string>

namespace veyra::source {
enum class DolbyBaseLayer : uint8_t { None, Unsupported, Hdr10, Sdr, Hlg };
struct DolbyVisionInfo {
    bool present=false, baseLayer=false, enhancementLayer=false, rpuDeclared=false, rpuObserved=false;
    bool hlgCorrected=false;   // PushFPS: correctHlgBase() acted on this file (said once in the log)
    uint8_t profile=0, level=0, compatibility=0;
    DolbyBaseLayer route() const {
        if(!present)return DolbyBaseLayer::None;
        if(!baseLayer)return DolbyBaseLayer::Unsupported;
        if(profile==7)return DolbyBaseLayer::Hdr10;
        if(profile!=8&&profile!=10)return DolbyBaseLayer::Unsupported;
        switch(compatibility){
        case 1:return DolbyBaseLayer::Hdr10;
        case 2:return DolbyBaseLayer::Sdr;
        case 4:return DolbyBaseLayer::Hlg;
        default:return DolbyBaseLayer::Unsupported;
        }
    }
    // PushFPS 4.0. Profile 8.4 says its base layer is HLG, and some encoders write PQ into the HEVC
    // colour description all the same. Read as PQ the highlights are crushed, and until 4.0 the file
    // was refused outright (matches() below). This corrects that one contradiction and nothing else:
    //  - only profile 8 with a base layer and compatibility 4 (never every profile 8, never profile 10,
    //    never a stream without the Dolby Vision configuration);
    //  - only a transfer that says PQ, or says nothing; a stream that says anything else is not the
    //    stream the configuration describes and stays refused;
    //  - only when matrix and primaries already are the HLG base layer's (BT.2020).
    // True when it changed the transfer. It is still compatibility playback of the base layer: the
    // RPU is not applied and nothing is output as Dolby Vision.
    bool correctHlgBase(pipeline::ColorDescription& c) const {
        using namespace pipeline;
        if(!present||!baseLayer||profile!=8||compatibility!=4)return false;
        if(c.transfer==TransferFunction::HLG)return false;
        if(c.transfer!=TransferFunction::PQ&&c.transfer!=TransferFunction::Unknown)return false;
        if(c.matrix!=YuvMatrix::BT2020NCL||c.primaries!=ColorPrimaries::BT2020)return false;
        c.transfer=TransferFunction::HLG;c.transferAssumed=true;return true;
    }
    std::wstring description() const {
        if(!present)return {};
        const auto layer=route();
        const wchar_t* name=layer==DolbyBaseLayer::Hdr10?L"HDR10":layer==DolbyBaseLayer::Hlg?L"HLG":L"SDR";
        if(layer==DolbyBaseLayer::Unsupported)return L"Dolby Vision P"+std::to_wstring(profile)+L"：此类型需要 RPU 重建，当前不能正确显示";
        return L"Dolby Vision P"+std::to_wstring(profile)+L" · "+name+L" 基础层兼容（不应用 RPU/增强层，不输出原生 DV）";
    }
    bool matches(const pipeline::ColorDescription& c) const {
        using namespace pipeline;
        switch(route()){
        case DolbyBaseLayer::Hdr10:return c.transfer==TransferFunction::PQ&&c.matrix==YuvMatrix::BT2020NCL&&c.primaries==ColorPrimaries::BT2020;
        case DolbyBaseLayer::Hlg:return c.transfer==TransferFunction::HLG&&c.matrix==YuvMatrix::BT2020NCL&&c.primaries==ColorPrimaries::BT2020;
        case DolbyBaseLayer::Sdr:return !c.isHdrPath()&&c.matrix==YuvMatrix::BT709&&c.primaries==ColorPrimaries::BT709;
        case DolbyBaseLayer::None:return true;
        default:return false;
        }
    }
};
}
