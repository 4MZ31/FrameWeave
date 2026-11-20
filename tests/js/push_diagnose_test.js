// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Check my setup: the rules of qml/Veyra/PushDiagnose.js.
'use strict'
const t = require('./load.js')
const D = t.load('PushDiagnose.js')
const ids = s => D.findings(s).map(f => f.id)
const find = (s, id) => D.findings(s).find(f => f.id === id)

// A session in which nothing is wrong: a 4K console picture at 30 FPS, NVIDIA VFG on High at 4X to
// 120 on a 120 Hz variable-refresh screen, enhancement at the picture's own size, sound connected.
function good(extra) {
    return Object.assign({
        playing: true, kind: 'capture', fgPossible: true, fgOn: true, presentsOwnFrames: false,
        multiplier: 4, method: 'vfg', vfgQuality: 2, pushFlowTier: 2, pushFlowMotion: 0, pushFlowP95Ms: 0,
        gameFps: 30, realFps: 30, generatedFps: 90, fgIdle: 0, stillOn: false,
        enhanceOn: true, enhanceSized: true, enhanceSize: 1, pictureHeight: 2160,
        targetFixed: true, targetFps: 120, outputCap: 120, refreshHz: 120, vsync: true, vrr: true,
        sharperFps: 0, sharperPixel: '', audioChoice: 2, audioSuggestion: 2
    }, extra || {})
}

t.same(ids(good()), [], 'a session with nothing wrong has no findings')
t.same(ids(null), [], 'no state: nothing')
t.same(ids(good({ playing: false, stillOn: true, enhanceSize: 0 })), [], 'nothing is judged while nothing plays')
t.same(ids({ playing: true }), [], 'a state with no numbers is never a finding')

// Keep the HUD sharp.
t.same(ids(good({ stillOn: true })), ['still-on'], 'the still rule on is said')
t.same(ids(good({ stillOn: true, fgOn: false })), ['fg-off'], 'without frame generation the still rule does nothing and is not said')
t.same(ids(good({ stillOn: true, presentsOwnFrames: true })), [], 'XeSS presents its own frames: the rule never acts there')

// Enhancement at 1080p on a larger picture.
t.same(find(good({ enhanceSize: 0 }), 'enhance-small').offer, [1, 5], 'a 4K picture is offered full size and 1440p')
t.same(find(good({ enhanceSize: 0, pictureHeight: 1440 }), 'enhance-small').offer, [1], 'a 1440p picture is offered full size')
t.same(ids(good({ enhanceSize: 0, pictureHeight: 1080 })), [], 'a 1080p picture is already worked out whole')
t.same(ids(good({ enhanceSize: 5 })), [], 'a size somebody chose is left alone')
t.same(ids(good({ enhanceSize: 0, enhanceOn: false })), [], 'enhancement off: nothing to say')
t.same(ids(good({ enhanceSize: 0, enhanceSized: false })), [], 'the AMD enhancement runtime has no size to choose')

// NVIDIA VFG's model.
t.same(find(good({ vfgQuality: 1 }), 'vfg-quality').tier, 1, 'VFG on Medium is offered High')
t.same(find(good({ vfgQuality: 0 }), 'vfg-quality').tier, 0, 'VFG on Low is offered High')
t.same(ids(good({ vfgQuality: 3 })), [], 'a mixed tier was chosen in Professional mode and is left alone')
t.same(ids(good({ vfgQuality: 1, method: 'dlss' })), [], 'another method has no such tier')
t.check(find(good({ vfgQuality: 1 }), 'vfg-quality').level === 'try', 'a change with a price is something to try')

// PushFlow with room.
const flow = extra => good(Object.assign({ method: 'pushflow', multiplier: 2, realFps: 30, generatedFps: 30, targetFps: 60, outputCap: 60, refreshHz: 60 }, extra))
t.same(ids(flow({ pushFlowTier: 2, pushFlowP95Ms: 2 })), [], 'the Quality tier has nowhere to go')
t.same(find(flow({ pushFlowTier: 0, pushFlowP95Ms: 8 }), 'pushflow-room').percent, 24, 'Fast using 24 % of a frame: Balanced is worth trying')
t.same(ids(flow({ pushFlowTier: 0, pushFlowP95Ms: 14 })), [], 'Fast using 42 % of a frame: no room')
t.same(ids(flow({ pushFlowTier: 1, pushFlowP95Ms: 8 })), [], 'Balanced using 24 %: the Quality tier would not fit')
t.same(find(flow({ pushFlowTier: 1, pushFlowP95Ms: 3 }), 'pushflow-room').tier, 1, 'Balanced using 9 %: Quality is worth trying')
t.same(ids(flow({ pushFlowTier: 0, pushFlowP95Ms: 0 })), [], 'no timing yet: no finding')
t.same(ids(flow({ pushFlowTier: 0, pushFlowP95Ms: 8, pushFlowMotion: 720 })), [], 'a motion size somebody chose is left alone')
t.same(ids(flow({ pushFlowTier: 0, pushFlowP95Ms: 4.4, multiplier: 4, generatedFps: 30 })), [], 'three generated frames of 4.4 ms are 40 % of a 30 FPS frame: no room')
t.same(find(flow({ pushFlowTier: 0, pushFlowP95Ms: 3, multiplier: 4, generatedFps: 30 }), 'pushflow-room').percent, 27, 'the share counts every generated frame of a pair')

// Frames asked for and not shown.
t.same(ids(good({ fgIdle: 3 })), ['fg-idle'], 'frame generation that shows nothing is the first thing said')
t.same(ids(good({ fgIdle: 1 })), [], 'nothing to add (a setting) is not a fault')
t.same(find(good({ generatedFps: 50 }), 'fg-behind').shown, 80, 'half the generated frames missing: the card is behind')
t.same(find(good({ generatedFps: 50 }), 'fg-behind').wanted, 120, 'and the target is said')
t.same(ids(good({ generatedFps: 80 })), [], 'a few frames short is not a finding')
t.same(ids(good({ realFps: 4, generatedFps: 0 })), [], 'a still picture (a menu) is not judged')
t.same(ids(good({ generatedFps: -1 })), [], 'no count of generated frames: not judged')
t.same(ids(good({ refreshHz: 60, outputCap: 0, targetFixed: false, generatedFps: 31 })), [], 'a 60 Hz screen can show 30 generated frames beside 30 real ones, whatever the multiplier')
t.same(ids(good({ presentsOwnFrames: true, generatedFps: 0 })), [], 'XeSS frames are not counted, so never judged')

// The target and the screen.
t.same(find(good({ refreshHz: 60, generatedFps: 30 }), 'target-above-screen').hz, 60, '120 FPS on a 60 Hz screen with vertical sync')
t.same(ids(good({ targetFps: 50, outputCap: 50, generatedFps: 20, vrr: false })), ['target-uneven'], '50 FPS does not divide 120 Hz without variable refresh')
t.same(ids(good({ targetFps: 60, outputCap: 60, generatedFps: 30, vrr: false })), [], '60 divides 120')
t.same(ids(good({ targetFps: 50, outputCap: 50, generatedFps: 20, vrr: true })), [], 'variable refresh shows any rate evenly')

// The capture card.
t.same(find(good({ sharperFps: 30, sharperPixel: 'YUY2' }), 'capture-compressed').fps, 30, 'a sharper format at another rate is said')
t.same(ids(good({ kind: 'ps5', sharperFps: 30 })), [], 'only for a capture card')
t.same(ids(good({ audioChoice: -1, audioSuggestion: 3 })), ['no-sound'], 'no sound input chosen and the card has one')
t.same(ids(good({ audioChoice: -1, audioSuggestion: -1 })), [], 'no input found: nothing to offer')
t.same(ids(good({ audioChoice: -2, audioSuggestion: -2 })), [], 'sound from the video device is a choice')

// Frame generation off on a console.
t.same(ids(good({ fgOn: false })), ['fg-off'], 'a console picture without frame generation')
t.same(ids(good({ fgOn: false, kind: 'file' })), [], 'a film is not told to switch frame generation on')
t.same(ids(good({ fgOn: false, fgPossible: false })), [], 'a card that cannot generate frames is not told to')

// Order and counting.
const many = D.findings(good({ fgIdle: 3, stillOn: true, enhanceSize: 0, vfgQuality: 1 }))
t.same(many.map(f => f.id), ['fg-idle', 'still-on', 'enhance-small'], 'problems first; a method that shows nothing is not offered another tier')
t.same(D.count(many), { problem: 1, better: 2, 'try': 0, note: 0, act: 3 }, 'counted by what they ask')
t.same(D.count([]).act, 0, 'nothing found, nothing to do')

// The mean of sampled figures.
t.same(D.mean([{ a: 10 }, { a: 20 }, { a: 'x' }, {}], 'a', -1), 15, 'values that are not numbers are left out')
t.same(D.mean([], 'a', -1), -1, 'no samples: the fallback')
t.same(D.mean([{ a: null }, { a: '' }, { a: undefined }], 'a', 7), 7, 'a figure that is not known is not a zero')

t.done('push_diagnose_test')
