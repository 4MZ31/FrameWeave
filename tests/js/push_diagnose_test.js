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

// 4.4.5: FSR on a card that is behind is offered a cheaper motion estimate, and only then.
const fsr = extra => good(Object.assign({ method: 'fsr3', generatedFps: 50, motionFlow: true, flowQuality: 1, flowHalfPossible: true, flowHalf: false }, extra || {}))
t.same(ids(fsr()), ['fg-behind', 'fsr-lighter'], 'FSR behind with the standard estimate: the lighter one is offered')
t.same(find(fsr(), 'fsr-lighter').quality, 1, 'with the level it is on')
t.same(find(fsr(), 'fsr-lighter').half, true, 'and that half size is still open')
t.same(ids(fsr({ method: 'fsr4' })), ['fg-behind', 'fsr-lighter'], 'the same for FSR 4')
t.same(ids(fsr({ generatedFps: 90 })), [], 'a card that keeps up is offered nothing')
t.same(ids(fsr({ flowQuality: 0, flowHalf: true })), ['fg-behind'], 'already on the lightest: nothing more to offer')
t.same(find(fsr({ flowQuality: 0 }), 'fsr-lighter').half, true, 'performance level already, half size not yet: still offered')
t.same(ids(fsr({ flowQuality: 0, flowHalfPossible: false })), ['fg-behind'], 'half size is AMD\'s estimate only')
t.same(ids(fsr({ motionFlow: false })), ['fg-behind'], 'no motion estimate in use: nothing to make lighter')
t.same(ids(fsr({ flowQuality: undefined, flowHalfPossible: undefined, flowHalf: undefined })), ['fg-behind'], 'settings nobody reported are never a finding')
t.same(ids(good({ generatedFps: 50, motionFlow: true, flowQuality: 2, flowHalfPossible: true, flowHalf: false })), ['fg-behind'], 'never for NVIDIA VFG, which works its motion out itself')
t.same(ids(fsr({ method: 'pushflow', pushFlowTier: 2 })), ['fg-behind'], 'nor for PushFlow')
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

// A reading that is not there is not a zero (Number(null) and Number('') are 0 in JavaScript).
for (const missing of [null, undefined, '', '   ', NaN, Infinity, -Infinity, 'x', true, false, [], {}]) {
    const label = typeof missing === 'string' ? JSON.stringify(missing) : String(missing)
    t.same(D.num(missing, -1), -1, 'not a reading: ' + label)
    t.same(ids(good({ generatedFps: missing })), [], 'generated frames not known (' + label + '): never "falling behind"')
}
t.same(D.num(0, -1), 0, 'a real zero is a zero')
t.same(D.num('0', -1), 0, 'and so is one written as text')
t.same(D.num(' 42.5 ', -1), 42.5, 'a number written as text is read')
t.same(D.num(-3, 9), -3, 'a negative reading is a reading')
t.same(find(good({ generatedFps: 0 }), 'fg-behind').shown, 30, 'a measured zero is still said: no generated frame is shown')
t.same(ids(good({ realFps: null, generatedFps: 10 })), [], 'real frames not known: nothing is judged from them')
t.same(ids(good({ enhanceSize: null })), [], 'an enhancement size that is not known is not "1080p"')
t.same(ids(good({ enhanceSize: '' })), [], 'nor is an empty one')
t.same(ids(good({ pushFlowMotion: null, method: 'pushflow', pushFlowTier: null, pushFlowP95Ms: 2 })), [], 'a tier that is not known is not offered another')
t.same(ids(good({ audioChoice: null, audioSuggestion: 3 })), [], 'a sound choice that is not known is not "no sound"')

// The mean of sampled figures.
// 4.2.1: a picture clearly smaller than the screen, with nothing enlarging it but the window.
const small = extra => good(Object.assign({ pictureHeight: 1080, enhanceSize: 1, screenHeight: 2160, upscalePossible: true, upscaleOn: false }, extra || {}))
t.same(ids(small()), ['upscale-off'], 'a 1080p picture on a 4K screen is offered upscaling')
t.same(find(small(), 'upscale-off'), { id: 'upscale-off', level: 'try', height: 1080, screen: 2160 }, 'with both sizes, as something to try')
t.same(ids(small({ screenHeight: 1440 })), ['upscale-off'], '1080p on a 1440p screen too')
t.same(ids(small({ pictureHeight: 1440 })), ['upscale-off'], 'and 1440p on a 4K screen')
t.same(ids(small({ screenHeight: 1200 })), [], 'not for a screen a little larger than the picture')
t.same(ids(small({ pictureHeight: 2160 })), [], 'not for a picture as large as the screen')
t.same(ids(small({ upscaleOn: true })), [], 'not when upscaling is already on')
t.same(ids(small({ upscalePossible: false })), [], 'not when the card has no upscaler')
t.same([ids(small({ screenHeight: 0 })), ids(small({ screenHeight: undefined })), ids(small({ screenHeight: null })), ids(small({ screenHeight: '' }))], [[], [], [], []], 'a screen size nobody reported is never "larger"')
t.same([ids(small({ upscaleOn: undefined })), ids(small({ upscalePossible: undefined })), ids(small({ pictureHeight: 0 }))], [[], [], []], 'nor is a state nobody reported "off", "possible" or "small"')
t.same(ids(good()), [], 'a state from before 4.2.1 (no screen, no upscaling fields) says nothing new')
{
    // The same threshold as the quick setup row's (PushUpscale.js).
    const U = t.load('PushUpscale.js')
    let same = true
    for (const p of [0, 240, 360, 720, 1080, 1200, 1440, 2160]) for (const sc of [0, 1080, 1200, 1296, 1297, 1440, 1600, 2160])
        same = same && (ids(small({ pictureHeight: p, screenHeight: sc })).indexOf('upscale-off') >= 0) === U.gains(p, sc)
    t.check(same, 'Check my setup and the quick setup row agree on every size')
}

t.same(D.mean([{ a: 10 }, { a: 20 }, { a: 'x' }, {}], 'a', -1), 15, 'values that are not numbers are left out')
t.same(D.mean([], 'a', -1), -1, 'no samples: the fallback')
t.same(D.mean([{ a: null }, { a: '' }, { a: undefined }], 'a', 7), 7, 'a figure that is not known is not a zero')

t.done('push_diagnose_test')
