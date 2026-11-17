#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
"""Prepares PushFlow's runtime for a release build (CI only; nothing it downloads is committed).

  1. ONNX Runtime (DirectML build) and DirectML from nuget.org, pinned versions; the SHA-256 of each
     package is printed and checked against PINS when a value is set there.
  2. RIFE models (MIT weights by hzwer, ONNX by vs-mlrt) from vs-mlrt's "external-models" release: the
     pinned archives in MODEL_ARCHIVES; the newest lite model becomes pushflow-fast.onnx and the newest
     full model (no ensemble) pushflow-quality.onnx.
  3. Each model is checked on the CPU with a moving test pattern (identical frames come back, a moving
     square lands in the middle, well ahead of a plain blend) and shipped in full precision.

Writes:
  <out>/ort, <out>/dml               the extracted packages (headers for CMake)
  <out>/runtime/pushflow/            onnxruntime.dll, DirectML.dll, models/*.onnx, models.json, licences
Usage: python -I scripts/pushflow/prepare_runtime.py --out D:\\deps\\pushflow
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

ORT_ID, ORT_VERSION = "Microsoft.ML.OnnxRuntime.DirectML", "1.20.1"
DML_ID, DML_VERSION = "Microsoft.AI.DirectML", "1.15.4"
MODELS_REPO, MODELS_TAG = "AmusementClub/vs-mlrt", "external-models"
# The two RIFE archives PushFlow ships (chosen by the checks below on the first build, 2026-11-17):
# v4.25 lite for Fast/Balanced, v4.26 for Quality. Change both the list and the pins together.
MODEL_ARCHIVES = ["rife_v4.25_lite.7z", "rife_v4.26.7z"]
# SHA-256 of the packages and model archives; a missing entry is printed only.
PINS: dict[str, str | None] = {
    "Microsoft.ML.OnnxRuntime.DirectML.1.20.1.nupkg": "6763468507b7cfc777b1334b3e174c11a540ddacb7bd4354bc2e0ec89e56eec2",
    "Microsoft.AI.DirectML.1.15.4.nupkg": "4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9",
    "rife_v4.25_lite.7z": "7d53e29fff5e67345b19f4ce97dfd1e34b490eedc752df2236904fda1a13842c",
    "rife_v4.26.7z": "dfdabd84a2a3db773f87604b8cc255e94a6a72f13550d910ccd3b4ee2606cd4f",
}


MIT = """MIT License

Copyright (c) {year}{holder}

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def pinned(name: str, path: Path) -> None:
    digest = sha256(path)
    print(f"sha256 {digest}  {name}")
    want = PINS.get(name)
    if want and want != digest:
        sys.exit(f"::error::{name}: SHA-256 {digest} does not match the pinned {want}")


def nuget(package: str, version: str, work: Path, out: Path) -> Path:
    url = f"https://api.nuget.org/v3-flatcontainer/{package.lower()}/{version}/{package.lower()}.{version}.nupkg"
    target = work / f"{package}.{version}.nupkg"
    print(f"download {url}")
    urllib.request.urlretrieve(url, target)
    pinned(target.name, target)
    folder = out / ("ort" if package == ORT_ID else "dml")
    shutil.rmtree(folder, ignore_errors=True)
    with zipfile.ZipFile(target) as z:
        for name in z.namelist():
            if ".." in Path(name).parts or name.startswith("/"):
                sys.exit(f"::error::unsafe entry {name} in {target.name}")
        z.extractall(folder)
    return folder


def seven_zip() -> str:
    for candidate in (shutil.which("7z"), r"C:\Program Files\7-Zip\7z.exe"):
        if candidate and Path(candidate).exists():
            return candidate
    sys.exit("::error::7-Zip not found")


def model_archives(work: Path) -> list[Path]:
    listing = subprocess.run(["gh", "release", "view", MODELS_TAG, "-R", MODELS_REPO, "--json", "assets"],
                             check=True, capture_output=True, text=True).stdout
    assets = [a["name"] for a in json.loads(listing)["assets"]]
    rife = sorted(a for a in assets if re.search(r"rife", a, re.I) and a.endswith((".7z", ".zip")))
    print("RIFE archives in the release:", ", ".join(rife) or "(none)")
    missing = [a for a in MODEL_ARCHIVES if a not in assets]
    if missing:
        sys.exit(f"::error::vs-mlrt external-models no longer has {', '.join(missing)}")
    rife = MODEL_ARCHIVES
    out = []
    for name in rife:
        subprocess.run(["gh", "release", "download", MODELS_TAG, "-R", MODELS_REPO, "-p", name, "-D", str(work), "--clobber"], check=True)
        pinned(name, work / name)
        out.append(work / name)
    return out


def version_of(path: Path):
    m = re.search(r"rife_?v?(\d+)\.(\d+)", path.name, re.I)
    return (int(m.group(1)), int(m.group(2))) if m else None


def describe(model_path: Path):
    import onnx
    m = onnx.load(str(model_path), load_external_data=False)
    inputs = [i for i in m.graph.input if i.name not in {init.name for init in m.graph.initializer}]
    if len(inputs) != 1 or len(m.graph.output) < 1:
        return None
    dims = inputs[0].type.tensor_type.shape.dim
    out_dims = m.graph.output[0].type.tensor_type.shape.dim
    if len(dims) != 4 or len(out_dims) != 4:
        return None
    channels = dims[1].dim_value
    fixed = [d.dim_value for d in dims[2:]]
    if channels not in (7, 11) or out_dims[1].dim_value not in (0, 3) or any(fixed):
        return None
    return channels


def frame(w, h, square_x):
    import numpy as np
    y, x = np.mgrid[0:h, 0:w]
    base = (60 + 40.0 * x / w + 30.0 * y / h) / 255.0
    img = np.stack([base, base + 10 / 255.0, base + 20 / 255.0])
    dx, dy = x - square_x, y - h * 0.35
    inside = (dx >= 0) & (dx < 40) & (dy >= 0) & (dy < 40)
    light = ((dx.astype(int) // 8 + dy.astype(int) // 8) % 2) == 0
    for c, (lo, hi) in enumerate(((40, 235), (30, 200), (20, 60))):
        img[c][inside] = np.where(light[inside], hi, lo) / 255.0
    return img.astype(np.float32)


def tensor(a, b, t, channels):
    import numpy as np
    _, h, w = a.shape
    planes = [a, b, np.full((1, h, w), t, np.float32)]
    if channels == 11:
        y, x = np.mgrid[0:h, 0:w].astype(np.float32)
        planes += [np.full((1, h, w), 2.0 / (w - 1), np.float32), np.full((1, h, w), 2.0 / (h - 1), np.float32),
                   (2.0 * x / (w - 1) - 1.0)[None], (2.0 * y / (h - 1) - 1.0)[None]]
    return np.concatenate(planes)[None]


def psnr(a, b):
    import numpy as np
    mse = float(np.mean((np.clip(a, 0, 1) - np.clip(b, 0, 1)) ** 2))
    return 99.0 if mse < 1e-12 else 10 * np.log10(1.0 / mse)


def check_model(path: Path, channels: int) -> dict:
    import onnxruntime as ort
    w, h = 256, 192
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    name = session.get_inputs()[0].name
    first, second, middle = frame(w, h, 100), frame(w, h, 116), frame(w, h, 108)
    same = session.run(None, {name: tensor(first, first, 0.5, channels)})[0][0]
    mid = session.run(None, {name: tensor(first, second, 0.5, channels)})[0][0]
    blend = (first + second) / 2
    return {"identical_db": round(psnr(same, first), 2), "moving_db": round(psnr(mid, middle), 2),
            "blend_db": round(psnr(blend, middle), 2), "output": mid}


def detail_frame(w, h, shift):
    """Fine texture panning sideways (stripes, 2-pixel checker, thin lines): what the Fast tier must keep."""
    import numpy as np
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    u = x - shift
    tex = 0.5 + 0.18 * np.sin(u * 1.3) + 0.12 * ((np.floor(u / 2) + np.floor(y / 2)) % 2) + 0.15 * (np.abs((u % 24) - 12) < 0.8)
    img = np.stack([tex, tex * 0.9 + 0.05, tex * 0.8 + 0.1])
    return np.clip(img, 0, 1).astype(np.float32)


def resize_box(img, f):
    import numpy as np
    c, h, w = img.shape
    return img.reshape(c, h // f, f, w // f, f).mean(axis=(2, 4)).astype(np.float32)


def bilinear_at(field, qy, qx):
    """field [C, h, w] sampled at continuous pixel coordinates (pixel centres at integers), clamped."""
    import numpy as np
    c, h, w = field.shape
    qx = np.clip(qx, 0, w - 1); qy = np.clip(qy, 0, h - 1)
    x0 = np.floor(qx).astype(np.int64); y0 = np.floor(qy).astype(np.int64)
    x1 = np.minimum(x0 + 1, w - 1); y1 = np.minimum(y0 + 1, h - 1)
    fx = qx - x0; fy = qy - y0
    a = field[:, y0, x0] * (1 - fx) + field[:, y0, x1] * fx
    b = field[:, y1, x0] * (1 - fx) + field[:, y1, x1] * fx
    return a * (1 - fy) + b * fy


def rebuild_scaled(img0, img1, grid0, grid1, mask, align, mask_for_first, model_hw):
    """What shaders/PushFlowReconstruct.hlsl does: grids and mask from the model size, frames at full size."""
    import numpy as np
    import rife_graph
    _, H, W = img0.shape
    mh, mw = model_hw
    ph, pw = rife_graph.to_hw2(grid0).shape[:2]
    yo, xo = np.mgrid[0:H, 0:W].astype(np.float64)
    qy = (yo + 0.5) * mh / H - 0.5; qx = (xo + 0.5) * mw / W - 0.5
    out = []
    for img, grid in ((img0, grid0), (img1, grid1)):
        g = np.moveaxis(rife_graph.to_hw2(grid), -1, 0)
        gx, gy = bilinear_at(g, qy, qx)
        if align:
            xm = (gx + 1) * 0.5 * (pw - 1); ym = (gy + 1) * 0.5 * (ph - 1)
        else:
            xm = ((gx + 1) * pw - 1) * 0.5; ym = ((gy + 1) * ph - 1) * 0.5
        xs = (xm + 0.5) * W / mw - 0.5; ys = (ym + 0.5) * H / mh - 0.5
        out.append(bilinear_at(img, ys, xs))
    m = bilinear_at(np.asarray(mask)[0], qy, qx)[0]
    return out[0] * m + out[1] * (1 - m) if mask_for_first else out[0] * (1 - m) + out[1] * m


def check_exposed(path: Path, channels: int, align: int) -> dict:
    import numpy as np
    import onnxruntime as ort
    import rife_graph
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    name = session.get_inputs()[0].name
    w, h = 256, 192
    first, second = frame(w, h, 100), frame(w, h, 116)
    rgb, g0, g1, m = session.run(None, {name: tensor(first, second, 0.5, channels)})[:4]
    a = rife_graph.reconstruct(first, second, g0, g1, m, align, True)
    b = rife_graph.reconstruct(first, second, g0, g1, m, align, False)
    pa, pb = rife_graph.psnr(a, rgb[0]), rife_graph.psnr(b, rgb[0])
    first_mask = pa >= pb
    # Fast tier: the network at half size, rebuilt at full size versus the half-size picture enlarged.
    W, H = 512, 384
    f0, f1, gt = detail_frame(W, H, 0), detail_frame(W, H, 12), detail_frame(W, H, 6)
    s0, s1 = resize_box(f0, 2), resize_box(f1, 2)
    srgb, sg0, sg1, sm = session.run(None, {name: tensor(s0, s1, 0.5, channels)})[:4]
    yo, xo = np.mgrid[0:H, 0:W].astype(np.float64)
    old = bilinear_at(srgb[0], (yo + 0.5) / 2 - 0.5, (xo + 0.5) / 2 - 0.5)
    new = rebuild_scaled(f0, f1, sg0, sg1, sm, align, first_mask, (H // 2, W // 2))
    full = session.run(None, {name: tensor(f0, f1, 0.5, channels)})[0][0]
    fast = {"old_enlarged_db": round(rife_graph.psnr(old, gt), 2), "full_res_rebuild_db": round(rife_graph.psnr(new, gt), 2),
            "full_size_network_db": round(rife_graph.psnr(full, gt), 2), "blend_db": round(rife_graph.psnr((f0 + f1) / 2, gt), 2)}
    return {"rebuild_db": round(max(pa, pb), 2), "mask_for_first": bool(first_mask), "fast_detail": fast, "output": rgb[0]}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    out = Path(args.out).resolve()
    work = out / "download"
    work.mkdir(parents=True, exist_ok=True)
    runtime = out / "runtime" / "pushflow"
    shutil.rmtree(runtime, ignore_errors=True)
    (runtime / "models").mkdir(parents=True)
    licences = runtime / "licenses"
    licences.mkdir()

    nuget(ORT_ID, ORT_VERSION, work, out)
    nuget(DML_ID, DML_VERSION, work, out)
    ort_dll = out / "ort" / "runtimes" / "win-x64" / "native" / "onnxruntime.dll"
    dml_dll = out / "dml" / "bin" / "x64-win" / "DirectML.dll"
    for f in (ort_dll, dml_dll, out / "ort" / "build" / "native" / "include" / "onnxruntime_c_api.h",
              out / "ort" / "build" / "native" / "include" / "dml_provider_factory.h", out / "dml" / "include" / "DirectML.h"):
        if not f.exists():
            sys.exit(f"::error::missing {f}")
    shutil.copy2(ort_dll, runtime)
    shutil.copy2(dml_dll, runtime)
    # ONNX Runtime's NuGet has no LICENSE.txt at its root in every version: use whichever LICENSE file it
    # has, else write its MIT licence (Copyright (c) Microsoft Corporation), so the licence always ships.
    ort_licence = next((f for f in sorted((out / "ort").glob("LICENSE*")) if f.is_file()), None)
    if ort_licence:
        shutil.copy2(ort_licence, licences / "ONNX_Runtime_LICENSE.txt")
    else:
        (licences / "ONNX_Runtime_LICENSE.txt").write_text(MIT.format(holder="Microsoft Corporation", year=""), encoding="utf-8")
    (licences / "RIFE_LICENSE.txt").write_text(
        "RIFE (Real-Time Intermediate Flow Estimation), Practical-RIFE v4 weights: https://github.com/hzwer/Practical-RIFE\n"
        "ONNX conversions: https://github.com/AmusementClub/vs-mlrt (external-models)\n\n" + MIT.format(holder="hzwer", year="2021 "),
        encoding="utf-8")
    for src, dst in ( (out / "ort" / "ThirdPartyNotices.txt", "ONNX_Runtime_ThirdPartyNotices.txt"),
                     (out / "dml" / "LICENSE.txt", "DirectML_LICENSE.txt"), (out / "dml" / "ThirdPartyNotices.txt", "DirectML_ThirdPartyNotices.txt")):
        if src.exists():
            shutil.copy2(src, licences / dst)
        else:
            print(f"note: {src.name} not in the package")

    # Models.
    extracted = work / "models"
    shutil.rmtree(extracted, ignore_errors=True)
    extracted.mkdir()
    z = seven_zip()
    for archive in model_archives(work):
        target = extracted / archive.stem
        subprocess.run([z, "x", "-y", f"-o{target}", str(archive)], check=True, stdout=subprocess.DEVNULL)
    candidates = []
    for path in sorted(extracted.rglob("*.onnx")):
        version = version_of(path)
        channels = describe(path)
        print(f"model {path.relative_to(extracted)} version={version} channels={channels}")
        if not version or not channels or "ensemble" in path.name.lower():
            continue
        lite = "lite" in path.name.lower()
        candidates.append({"path": path, "version": version, "channels": channels, "lite": lite})
    chosen = {}
    for role, lite in (("fast", True), ("quality", False)):
        pool = [c for c in candidates if c["lite"] == lite and c["version"] < (4, 30)]
        # Newest version; at equal versions the 7-channel (grid inside the model) layout.
        pool.sort(key=lambda c: (c["version"], c["channels"] == 7), reverse=True)
        for c in pool:
            result = check_model(c["path"], c["channels"])
            print(f"check {role} {c['path'].name}: identical {result['identical_db']} dB, moving {result['moving_db']} dB, blend {result['blend_db']} dB")
            if result["identical_db"] > 30 and result["moving_db"] > result["blend_db"] + 2:
                chosen[role] = dict(c, check=result)
                break
        if role not in chosen and role == "fast" and candidates:
            print("no lite model passed; the fast tier uses the full model at its reduced size")
        if role not in chosen and role == "quality" and "fast" in chosen:
            chosen["quality"] = chosen["fast"]
    if "fast" not in chosen and "quality" in chosen:
        chosen["fast"] = chosen["quality"]
    if "fast" not in chosen:
        sys.exit("::error::no RIFE model passed the checks")

    # Every shipped model is the vs-mlrt export with the final warp grids and blend mask added as outputs
    # (rife_graph.expose), checked on the CPU: rebuilding from them reproduces the model's own picture, and
    # for the Fast tier (network at half size) rebuilding at full size from the full-size frames keeps more
    # detail than enlarging the network's half-size picture. Full precision is the reference; a
    # half-precision copy (convolutions fp16, warps, resizes, sigmoid and blend fp32) ships next to it when it
    # agrees on the CPU, and PushFlow only uses it after its own check on the user's graphics card.
    import onnx
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import rife_graph
    record = {"source": f"https://github.com/{MODELS_REPO}/releases/tag/{MODELS_TAG}", "runtime": f"{ORT_ID} {ORT_VERSION}, {DML_ID} {DML_VERSION}",
              "contract": {"input": "[1, C, H, W] float32, C = 7 (img0 RGB, img1 RGB, t) or 11 (+ flow scale, grid)", "pad_to": 64,
                           "color": "0..1, the encoding PushFlowPack writes", "outputs": "rgb [1,3,H,W]; pushflow_grid0/1 (normalized -1..1); pushflow_mask [1,1,H,W]"},
              "models": {}}
    for role, c in chosen.items():
        base = onnx.load(str(c["path"]))
        exposed, info = rife_graph.expose(base)
        target = runtime / "models" / f"pushflow-{role}.onnx"
        onnx.save(exposed, str(target))
        verdict = check_exposed(target, c["channels"], info["align_corners"])
        print(f"exposed {role}: {json.dumps({k: v for k, v in verdict.items() if k != 'output'})}")
        if verdict["rebuild_db"] < 40:
            sys.exit(f"::error::rebuilding from grids and mask does not reproduce {c['path'].name} ({verdict['rebuild_db']} dB)")
        entry = {"file": target.name, "from": c["path"].name, "version": "%d.%d" % c["version"], "lite": c["lite"],
                 "channels": c["channels"], "precision": "fp32", "sha256": sha256(target), "opset": [o.version for o in base.opset_import],
                 "align_corners": info["align_corners"], "padding_mode": info["padding_mode"], "mask_for_first": verdict["mask_for_first"],
                 "cpu_check_db": {k: c["check"][k] for k in ("identical_db", "moving_db", "blend_db")},
                 "rebuild_db": verdict["rebuild_db"], "fast_detail": verdict["fast_detail"]}
        try:
            from onnxconverter_common import float16
            half = float16.convert_float_to_float16(exposed, keep_io_types=True,
                                                    op_block_list=list(float16.DEFAULT_OP_BLOCK_LIST) + ["GridSample", "Resize", "Sigmoid", "Softmax"])
            half_path = runtime / "models" / f"pushflow-{role}-fp16.onnx"
            onnx.save(half, str(half_path))
            hv = check_exposed(half_path, c["channels"], info["align_corners"])
            agreement = rife_graph.psnr(hv["output"], verdict["output"])
            print(f"fp16 {role}: agreement with fp32 {agreement:.1f} dB")
            if agreement > 40 and hv["rebuild_db"] >= 40:
                entry["fp16"] = {"file": half_path.name, "sha256": sha256(half_path), "cpu_agreement_db": round(agreement, 2)}
            else:
                half_path.unlink()
        except Exception as e:  # noqa: BLE001 - no fp16 copy; fp32 stays the only model
            print(f"fp16 {role} skipped: {e}")
        record["models"][role] = entry
    (runtime / "models.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, indent=2))
    for f in sorted(runtime.rglob("*")):
        if f.is_file():
            print(f"{f.stat().st_size:>12}  {f.relative_to(runtime)}")


if __name__ == "__main__":
    main()
