import numpy as np, subprocess, os, sys
rng=np.random.default_rng(7)
W,H=1920,1080
def pink(beta,w=W+256,h=H):
    fy=np.fft.fftfreq(h)[:,None]; fx=np.fft.rfftfreq(w)[None,:]
    f=np.sqrt(fx*fx+fy*fy); f[0,0]=1
    spec=(rng.standard_normal((h,w//2+1))+1j*rng.standard_normal((h,w//2+1)))/f**beta
    spec[0,0]=0
    img=np.fft.irfft2(spec,s=(h,w)); img/=img.std(); return img
def frames(tex,sigma,d,blur):
    # the world as a wide texture; the camera pans d px a frame; a 30 fps game's motion blur smears along the pan
    def view(off):
        v=tex[:, :]
        if blur>0:
            n=max(1,int(round(blur*d)))
            acc=np.zeros((H,W))
            for k in range(n): acc+=tex[:, int(off)+k: int(off)+k+W]
            return acc/n
        return tex[:, int(off): int(off)+W]
    a=view(0); b=view(d); m=view(d//2)
    q=lambda x: np.clip(np.round(128+sigma*x),0,255)
    return q(a),q(b),q(m)
def rule(same):
    p='/tmp/still_same.bin'; same.astype(np.uint8).tofile(p)
    r=subprocess.run([os.path.join(os.path.dirname(os.path.abspath(__file__)),'sim'),p,str(W),str(H)]); assert r.returncode==0,r.returncode
    return [np.fromfile(p+ext,dtype=np.uint8).reshape(H,W).astype(bool) for ext in ('.k40','.k41')]
print('A moving picture (a pan). Share of pixels the rule overwrites with the old real frame, and how wrong they are there (of 255):')
print(f"{'texture contrast':>17} {'pan px/frame':>12} {'motion blur':>11} | {'matching':>8} | {'4.0 kept':>8} {'err':>5} | {'4.1 kept':>8} {'err':>5}")
for beta in (1.0,):
  tex=pink(beta)
  for sigma in (3,6,12,24):
    for d in (6,16,40):
      for blur in (0,0.5):
        a,b,m=frames(tex,sigma,d,blur)
        # three channels: the same picture with a little independent colour variation, as a camera/codec chain leaves
        same=np.abs(a-b)<=2
        k40,k41=rule(same)
        e=np.abs(a-m)
        f=lambda k:(100*k.mean(), e[k].mean() if k.any() else 0)
        print(f"{sigma:>17} {d:>12} {('yes' if blur else 'no'):>11} | {100*same.mean():7.1f}% | {f(k40)[0]:7.2f}% {f(k40)[1]:5.1f} | {f(k41)[0]:7.2f}% {f(k41)[1]:5.1f}")
# Poles against a sky, over a textured town: the pan moves poles further than their width.
print('\nPoles in front of a plain sky during a pan (pole 6 px wide, pan 24 px a frame): share of each pole the rule erases in the generated frame')
tex=pink(1.0)
def scene(off):
    img=np.empty((H,W)); yy=np.arange(H)[:,None]
    sky=200-30*(yy/H)                      # a smooth gradient
    img[:]=sky
    town=128+18*tex[:, off:off+W]
    skyline=(620+60*np.sin((np.arange(W)+off)/90.0)).astype(int)
    for x in range(W): img[skyline[x]:,x]=town[skyline[x]:,x]
    polemask=np.zeros((H,W),bool)
    for px in range(120,W+400,260):
        x0=px-off
        if 0<=x0<W-6: img[180:,x0:x0+6]=np.minimum(img[180:,x0:x0+6],40); polemask[180:560,x0:x0+6]=True
    return np.clip(np.round(img),0,255),polemask
a,_=scene(0); b,_=scene(24); m,pm=scene(12)
same=np.abs(a-b)<=2
k40,k41=rule(same)
for name,k in (('4.0',k40),('4.1 (test build)',k41)):
    erased=(k&pm).sum()/pm.sum()*100
    print(f"  {name:>17}: {erased:5.1f}% of the poles' pixels above the skyline replaced by sky; {100*k.mean():.2f}% of the whole frame overwritten")
