#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
"""RIFE graph tools for PushFlow (used by prepare_runtime.py; CI only).

RIFE v4 ends by warping both real frames with their final flow (two GridSample nodes) and blending them
with a sigmoid mask:  out = warp(img0, grid0) * mask + warp(img1, grid1) * (1 - mask).
expose() adds grid0, grid1 and mask as extra model outputs, so PushFlow can rebuild the in-between frame
itself: at full resolution from the full-resolution real frames even when the network ran at a reduced
size (the Fast tier), instead of enlarging the network's small RGB picture. check_reconstruction() proves,
on the CPU, that rebuilding from the exposed tensors reproduces the model's own output.
"""
from __future__ import annotations

import collections

import numpy as np


def _producers(graph):
    return {o: n for n in graph.node for o in n.output}


def find_final_blend(model):
    """Returns (grid0, grid1, mask, align_corners, padding_mode) for the last warps and mask before output 0."""
    graph = model.graph
    producer = _producers(graph)
    out = graph.output[0].name
    dist = {out: 0}
    queue = collections.deque([out])
    order = []
    while queue:
        name = queue.popleft()
        node = producer.get(name)
        if node is None:
            continue
        order.append((dist[name], node))
        for i in node.input:
            if i and i not in dist:
                dist[i] = dist[name] + 1
                queue.append(i)
    warps = [n for _, n in order if n.op_type == "GridSample"]
    sigmoids = [n for _, n in order if n.op_type == "Sigmoid"]
    if len(warps) < 2 or not sigmoids:
        raise RuntimeError(f"no final blend found ({len(warps)} GridSample, {len(sigmoids)} Sigmoid near the output)")
    w0, w1 = warps[0], warps[1]
    attrs = {a.name: a for a in w0.attribute}
    align = int(attrs["align_corners"].i) if "align_corners" in attrs else 0
    pad = attrs["padding_mode"].s.decode() if "padding_mode" in attrs else "zeros"
    return w0.input[1], w1.input[1], sigmoids[0].output[0], align, pad, (w0.input[0], w1.input[0])


def expose(model):
    """A copy of the model with grid0, grid1 and mask as outputs 1-3 (named pushflow_grid0/1, pushflow_mask)."""
    import onnx
    from onnx import helper
    m = onnx.ModelProto()
    m.CopyFrom(model)
    g0, g1, mask, align, pad, _ = find_final_blend(m)
    for src, name in ((g0, "pushflow_grid0"), (g1, "pushflow_grid1"), (mask, "pushflow_mask")):
        m.graph.node.append(helper.make_node("Identity", [src], [name], name=name + "_out"))
        m.graph.output.append(helper.make_tensor_value_info(name, onnx.TensorProto.FLOAT, None))
    return m, {"align_corners": align, "padding_mode": pad}


def grid_sample(img, grid, align_corners, padding="border"):
    """numpy GridSample, bilinear: img [C,H,W], grid [H',W',2] in -1..1 (x, y)."""
    c, h, w = img.shape
    gx, gy = grid[..., 0], grid[..., 1]
    if align_corners:
        x = (gx + 1) * 0.5 * (w - 1)
        y = (gy + 1) * 0.5 * (h - 1)
    else:
        x = ((gx + 1) * w - 1) * 0.5
        y = ((gy + 1) * h - 1) * 0.5
    x = np.clip(x, 0, w - 1)
    y = np.clip(y, 0, h - 1)
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    x1 = np.minimum(x0 + 1, w - 1)
    y1 = np.minimum(y0 + 1, h - 1)
    fx, fy = x - x0, y - y0
    a = img[:, y0, x0] * (1 - fx) + img[:, y0, x1] * fx
    b = img[:, y1, x0] * (1 - fx) + img[:, y1, x1] * fx
    return a * (1 - fy) + b * fy


def to_hw2(grid):
    """Grid tensor -> [H, W, 2] whatever the export's layout ([1,H,W,2] or [1,2,H,W])."""
    g = np.asarray(grid)[0]
    return g if g.shape[-1] == 2 else np.moveaxis(g, 0, -1)


def reconstruct(img0, img1, grid0, grid1, mask, align_corners, mask_for_first=True):
    w0 = grid_sample(img0, to_hw2(grid0), align_corners)
    w1 = grid_sample(img1, to_hw2(grid1), align_corners)
    m = np.asarray(mask)[0, 0]
    return w0 * m + w1 * (1 - m) if mask_for_first else w0 * (1 - m) + w1 * m


def psnr(a, b):
    mse = float(np.mean((np.clip(a, 0, 1) - np.clip(b, 0, 1)) ** 2))
    return 99.0 if mse < 1e-12 else 10 * np.log10(1.0 / mse)
