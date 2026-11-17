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

    # Full precision: half-precision conversions passed the CPU check but gave wrong pictures through
    # DirectML on WARP (2026-11-17, PushGpuTests), while the full-precision models matched; until fp16 is
    # verified on real cards PushFlow ships fp32.
    record = {"source": f"https://github.com/{MODELS_REPO}/releases/tag/{MODELS_TAG}", "runtime": f"{ORT_ID} {ORT_VERSION}, {DML_ID} {DML_VERSION}", "models": {}}
    for role, c in chosen.items():
        target = runtime / "models" / f"pushflow-{role}.onnx"
        shutil.copy2(c["path"], target)
        record["models"][role] = {"file": target.name, "from": c["path"].name, "version": "%d.%d" % c["version"], "lite": c["lite"],
                                  "channels": c["channels"], "precision": "fp32", "sha256": sha256(target),
                                  "cpu_check_db": {k: c["check"][k] for k in ("identical_db", "moving_db", "blend_db")}}
    (runtime / "models.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, indent=2))
    for f in sorted(runtime.rglob("*")):
        if f.is_file():
            print(f"{f.stat().st_size:>12}  {f.relative_to(runtime)}")


if __name__ == "__main__":
    main()
