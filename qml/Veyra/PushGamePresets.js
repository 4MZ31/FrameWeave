// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS game presets: the built-in library and the share-code format. A preset
// holds the quick-setup choices (game frame rate, target) and up to four HUD
// regions, which PushFPS applies as "Exclude all effects" areas so the minimap and
// HUD stay sharp and are never interpolated. Coordinates are fractions of the
// captured picture (0..1, left/top/right/bottom).
.pragma library

var kCodePrefix = "PUSHFPS1:"
var kMaxRegions = 4
var kMaxName = 40

// Layouts here are estimates, said so in each description: a preset is a start
// that the player adjusts by drawing the areas in Professional mode.
var builtIn = [
    { id: "gta6", name: "GTA VI", fps: 30, target: "auto", feather: 6,
      note: "Minimap bottom left, money and wanted level top right. Layout estimated from the GTA series; check it once you play.",
      regions: [ { left: 0.012, top: 0.735, right: 0.178, bottom: 0.965, ellipse: false },
                 { left: 0.80, top: 0.02, right: 0.99, bottom: 0.14, ellipse: false } ] },
    { id: "open-world-bl", name: "Open world · minimap bottom left", fps: 30, target: "auto", feather: 6,
      note: "Round or square minimap in the bottom left corner.",
      regions: [ { left: 0.012, top: 0.72, right: 0.20, bottom: 0.97, ellipse: false } ] },
    { id: "open-world-tr", name: "Open world · minimap top right", fps: 30, target: "auto", feather: 6,
      note: "Minimap or compass in the top right corner.",
      regions: [ { left: 0.80, top: 0.025, right: 0.985, bottom: 0.30, ellipse: false } ] },
    { id: "action-corners", name: "Action · HUD in the bottom corners", fps: 30, target: "auto", feather: 6,
      note: "Health bottom left, weapon and ammo bottom right.",
      regions: [ { left: 0.012, top: 0.80, right: 0.30, bottom: 0.98, ellipse: false },
                 { left: 0.70, top: 0.80, right: 0.988, bottom: 0.98, ellipse: false } ] },
    { id: "racing", name: "Racing · speedometer bottom right", fps: 30, target: "auto", feather: 6,
      note: "Speed, gear and minimap in the bottom right corner.",
      regions: [ { left: 0.72, top: 0.68, right: 0.99, bottom: 0.98, ellipse: false } ] },
    { id: "shooter", name: "Shooter · HUD in all four corners", fps: 30, target: "auto", feather: 4,
      note: "Small elements in every corner.",
      regions: [ { left: 0.01, top: 0.02, right: 0.20, bottom: 0.16, ellipse: false },
                 { left: 0.80, top: 0.02, right: 0.99, bottom: 0.16, ellipse: false },
                 { left: 0.01, top: 0.84, right: 0.24, bottom: 0.98, ellipse: false },
                 { left: 0.76, top: 0.84, right: 0.99, bottom: 0.98, ellipse: false } ] },
    { id: "cinematic", name: "Cinematic · no HUD protection", fps: 30, target: "60", feather: 6,
      note: "Story games and films: the whole picture gets frame generation.",
      regions: [] }
]

function clamp01(v) { v = Number(v); return isFinite(v) ? Math.min(1, Math.max(0, v)) : 0 }

// A clean preset, or null. Accepts the share-code and stored shapes.
function sanitize(p) {
    if (!p || typeof p !== "object") return null
    var name = String(p.name || "").trim().slice(0, kMaxName)
    if (name.length === 0) return null
    var fps = Number(p.fps)
    var regions = []
    var src = Array.isArray(p.regions) ? p.regions : []
    for (var i = 0; i < src.length && regions.length < kMaxRegions; ++i) {
        var r = src[i] || {}
        var l = clamp01(r.left !== undefined ? r.left : r.l), t = clamp01(r.top !== undefined ? r.top : r.t)
        var rr = clamp01(r.right !== undefined ? r.right : r.r), b = clamp01(r.bottom !== undefined ? r.bottom : r.b)
        var q = { left: Math.min(l, rr), top: Math.min(t, b), right: Math.max(l, rr), bottom: Math.max(t, b),
                  ellipse: (r.ellipse !== undefined ? r.ellipse : r.e) === true }
        if (q.right - q.left >= 0.005 && q.bottom - q.top >= 0.005) regions.push(q)
    }
    var feather = Number(p.feather)
    return {
        id: typeof p.id === "string" ? p.id.slice(0, 60) : "",
        name: name,
        fps: fps === 40 || fps === 60 ? fps : 30,
        target: ["auto", "off", "60", "120", "monitor"].indexOf(p.target) >= 0 ? p.target : "auto",
        feather: isFinite(feather) ? Math.min(64, Math.max(0, feather)) : 6,
        note: String(p.note || "").slice(0, 200),
        regions: regions
    }
}

function round3(v) { return Math.round(v * 1000) / 1000 }

// "PUSHFPS1:" + base64 of the compact JSON (UTF-8 safe through URI encoding).
function encode(p, btoa) {
    var s = sanitize(p)
    if (!s) return ""
    var compact = { v: 1, name: s.name, fps: s.fps, target: s.target, feather: s.feather, note: s.note,
                    regions: s.regions.map(function (r) {
                        return { l: round3(r.left), t: round3(r.top), r: round3(r.right), b: round3(r.bottom), e: r.ellipse } }) }
    return kCodePrefix + btoa(unescape(encodeURIComponent(JSON.stringify(compact))))
}

// The preset in a share code or a .pushfps file (code or JSON), or null.
function decode(text, atob) {
    var t = String(text || "").trim()
    if (t.length === 0 || t.length > 20000) return null
    try {
        if (t.indexOf(kCodePrefix) === 0)
            return sanitize(JSON.parse(decodeURIComponent(escape(atob(t.slice(kCodePrefix.length).replace(/\s+/g, ""))))))
        return sanitize(JSON.parse(t))
    } catch (e) {
        return null
    }
}

// "auto": 120 FPS on a 120 Hz+ screen when the frame generation offers enough, else 60.
function resolveTarget(target, refreshHz) {
    if (target !== "auto") return target
    return refreshHz >= 119 ? "120" : "60"
}
