#include "NrProtection.hlsli"
#include "NrCorrection.hlsli"
Texture2D<float4> baseTex:register(t0);
Texture2D<float4> nrInput:register(t1);
Texture2D<float4> nrFinal:register(t2);
RWTexture2D<float4> outputTex:register(u0);
cbuffer Params:register(b0){
    float total;float darken;float brighten;float color;float luminance;float protectionEnabled;float featherPixels;float unused;float4 regions[4];
    float correctionEnabled;float hueProtection;float chromaProtection;float highlightProtection;
    float localCompression;float temporalStability;float correctionAutomatic;float correctionPad;
    float neutralProtection;float colorRetention;float luminanceRetention;float shadowProtection;
    float4 correctionReserved;
}
// Extrapolating a negative linear-light residual can cross zero even when
// both input images contain shadow detail. Continue below the unboosted
// endpoint with a positive, tangent-matched curve instead of hard clipping.
// Identity and 0..1 mixing remain exact. Signed HDR gamut components remain
// signed; this is a residual-strength safeguard, not an input/output gamma.
float shadowSafe(float base,float rawDelta,float requestedDelta){
    float anchorDelta=min(rawDelta,0.0);
    float anchor=base+anchorDelta;
    if(base>=0&&anchor>0&&requestedDelta<anchorDelta)
        return anchor/(1.0+(anchorDelta-requestedDelta)/anchor);
    return base+requestedDelta;
}
// PushFPS fine-tuning of the change (pipeline/PushEnhanceShape.h, which holds the same maths as the
// reference the tests compare against). correctionReserved carries it: w = 1 when there is any,
// x = shadows + 256 midtones + 65536 highlights, y = hue + 256 maxBrighten + 65536 maxDarken, whole
// percentages. Every other caller passes zeros there and gets exactly the code path it always had.
float PushPerceptual(float y,bool hdr){return pow(max(y,0.0)/(hdr?203.0/80.0:1.0),1.0/2.2);}
float PushLinear(float p,bool hdr){return pow(max(p,0.0),2.2)*(hdr?203.0/80.0:1.0);}
// Before the layer's own strength: the change scaled by the picture's tonal range, and the part of
// its colour change that turns the hue scaled by the hue value.
float3 PushShapeDelta(float3 base,float3 delta,uint zones,uint more,bool hdr){
    const float3 k=float3(0.2126,0.7152,0.0722);
    float shadows=float(zones&255)/100.0,midtones=float((zones>>8)&255)/100.0,highlights=float((zones>>16)&255)/100.0;
    uint hue=more&255;
    float yb=dot(base,k);
    float t=saturate(PushPerceptual(yb,hdr));
    delta*=shadows*(1-t)*(1-t)+midtones*2*t*(1-t)+highlights*t*t;
    if(hue!=100){
        float dy=dot(delta,k);float3 dc=delta-dy;
        float3 cb=base-yb;float len=length(cb);
        float weight=smoothstep(0.02,0.10,len/max(abs(yb),1e-4));
        if(len>1e-6&&weight>0){
            float3 dir=cb/len;float along=dot(dc,dir);
            dc=along*dir+(dc-along*dir)*(1.0+(float(hue)/100.0-1.0)*weight);
            delta=dy+dc;
        }
    }
    return delta;
}
// After the layer's own strength: the soft limits on brightening and darkening. The change in the
// pixel's brightness is limited to limit*tanh(change/limit), on the perceptual scale, and the whole
// change is scaled to match. Two things are written out here on purpose:
// - tanh, from exp with its argument clamped: with the intrinsic, the test on Windows' software
//   renderer passed a 5 % limit by 14 % (it was given arguments up to 20), and exp-based versions
//   of it overflow for large arguments;
// - the scale. The exact one divides by the change in luminance, so where brightness hardly changes
//   but colour does it would multiply pow's rounding by thousands. There the scale that is right to
//   first order is used, which is well conditioned, and the two are blended in between.
float3 PushCapDelta(float3 base,float3 delta,uint more,bool hdr){
    const float3 k=float3(0.2126,0.7152,0.0722);
    float yb=dot(base,k),dy=dot(delta,k);
    float cap=float(dy>0?(more>>8)&255:(more>>16)&255)/100.0;
    if(cap<=0||abs(dy)<=1e-7)return delta;
    float pb=PushPerceptual(yb,hdr),change=abs(PushPerceptual(yb+dy,hdr)-pb);
    float x=change/cap;
    float keep=x<0.1?1.0-x*x/3.0+2.0*x*x*x*x/15.0:(1.0-2.0/(exp(2.0*min(x,10.0))+1.0))/max(x,0.1);
    float blend=smoothstep(0.01,0.03,change);
    if(blend>0){
        float limited=(dy>0?1.0:-1.0)*change*keep;
        keep=lerp(keep,saturate((PushLinear(pb+limited,hdr)-yb)/dy),blend);
    }
    return delta*keep;
}
// Bilateral resampling of the change uses the preserved high-resolution base
// as the edge guide. It never interpolates the high-resolution base itself.
[numthreads(16,16,1)] void main(uint3 id:SV_DispatchThreadID){
    uint w,h,nw,nh;baseTex.GetDimensions(w,h);nrInput.GetDimensions(nw,nh);
    if(id.x>=w||id.y>=h)return;
    float4 base=baseTex[id.xy];
    float protection=NrProtection(float2(id.xy)+0.5,uint2(w,h),protectionEnabled,featherPixels,regions);
    if(total==0||protection>=1){outputTex[id.xy]=base;return;}
    float2 pos=(float2(id.xy)+0.5)*float2(nw,nh)/float2(w,h)-0.5;
    int2 origin=(int2)floor(pos);float2 f=frac(pos);float3 delta=0;float weights=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        int2 q=clamp(origin+int2(x,y),int2(0,0),int2(nw-1,nh-1));
        float3 low=nrInput[q].rgb;
        float weight=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        if(w!=nw||h!=nh)weight/=1+16*dot(abs(base.rgb-low),float3(0.2126,0.7152,0.0722));
        delta+=(nrFinal[q].rgb-low)*weight;weights+=weight;
    }
    delta/=max(weights,1e-6);
    float3 rawDelta=delta;
    if(darken!=1||brighten!=1)delta=min(delta,0)*darken+max(delta,0)*brighten;
    if(color!=1||luminance!=1){float dy=dot(delta,float3(0.2126,0.7152,0.0722));delta=dy*luminance+(delta-dy)*color;}
    bool pushShape=correctionReserved.w>0.5;
    uint pushZones=uint(correctionReserved.x+0.5),pushMore=uint(correctionReserved.y+0.5);
    if(pushShape)delta=PushShapeDelta(base.rgb,delta,pushZones,pushMore,unused>0.5);
    delta*=total*(1-protection);
    if(pushShape&&(pushMore>>8)!=0)delta=PushCapDelta(base.rgb,delta,pushMore,unused>0.5);
    float3 result=float3(shadowSafe(base.r,rawDelta.r,delta.r),
                         shadowSafe(base.g,rawDelta.g,delta.g),
                         shadowSafe(base.b,rawDelta.b,delta.b));
    if(correctionEnabled>0.5)
        result=NrApplyCorrection(base.rgb,result,unused>0.5,hueProtection,chromaProtection,highlightProtection,localCompression,
            neutralProtection,colorRetention,luminanceRetention,shadowProtection);
    outputTex[id.xy]=float4(unused>0.5?result:max(0,result),base.a);
}
