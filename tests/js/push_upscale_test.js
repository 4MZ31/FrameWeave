// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// "Upscale to my screen" (qml/Veyra/PushUpscale.js): what quick setup's row and Check my setup's offer set.
'use strict'
const t = require('./load.js')
const U = t.load('PushUpscale.js')

// When it is worth offering.
t.check(U.gains(1080, 2160) && U.gains(1080, 1440) && U.gains(1440, 2160) && U.gains(720, 1080), 'a picture clearly smaller than the screen gains')
t.check(!U.gains(2160, 2160) && !U.gains(1080, 1080) && !U.gains(1080, 1200) && !U.gains(2160, 1440), 'the same size, a little smaller, or larger does not')
t.check(!U.gains(0, 2160) && !U.gains(undefined, 2160) && !U.gains(null, 2160) && !U.gains('', 2160) && !U.gains(NaN, 2160) && !U.gains(240, 2160), 'a size not known is never "smaller"')
t.check(!U.gains(1080, 0) && !U.gains(1080, undefined) && !U.gains(1080, NaN) && !U.gains(1080, -5), 'nor is a screen not known')

// The choices as the bridge gives them: an NVIDIA card, an AMD card, an Intel card, nothing.
const nvidia = [{ id: '0', label: 'DLSS SR', disabled: false }, { id: 'rtx', label: 'RTX', disabled: false }, { id: 'rtx-hb', label: 'RTX HB', disabled: false },
                { id: 'rtx-st', label: 'RTX ST', disabled: false }, { id: '5', label: 'FSR 3.1', disabled: false }, { id: '6', label: 'NIS', disabled: false }]
const amd = nvidia.map(o => Object.assign({}, o, { disabled: !(o.id === '5' || o.id === '6') }))
amd[4].label = 'FSR 3.1 / 4'
const shaderOnly = nvidia.map(o => Object.assign({}, o, { disabled: o.id !== '6' }))
const none = nvidia.map(o => Object.assign({}, o, { disabled: true }))

t.same(U.offered(amd).map(o => o.id), ['5', '6'], 'only what the card can run is offered')
t.same(U.offered(null), [], 'no list, nothing offered')
t.same(U.offered([null, {}, { id: 7 }, { id: '', disabled: false }, { id: 'x' }]).map(o => o.id), ['x'], 'entries that are not choices are left out')

t.same(U.method(nvidia, 'rtx'), 'rtx', 'the upscaler already chosen is kept when the card runs it')
t.same(U.method(nvidia, '0'), '0', 'DLSS SR is kept on a card that runs it')
t.same(U.method(amd, 'rtx'), '5', 'an AMD card gets FSR in place of an NVIDIA upscaler')
t.same(U.method(amd, '0'), '5', 'and in place of DLSS SR')
t.same(U.method(amd, '6'), '6', 'NVIDIA Image Scaling chosen on an AMD card is kept: it is a shader')
t.same(U.method(shaderOnly, 'rtx'), '6', 'without FSR, NVIDIA Image Scaling')
t.same(U.method(none, 'rtx'), '', 'nothing to run: no upscaler')
t.same(U.method([{ id: 'future', disabled: false }], 'rtx'), 'future', 'an unknown list: its first choice')
t.same(U.method(amd, undefined), '5', 'no choice yet: one every card runs')
t.same(U.method(amd, 5), '5', 'a number is read as its text')

// The size.
t.same(U.targetIndex(2160, 2), 0, '4K screen, 4K target: nothing to change')
t.same(U.targetIndex(1440, 2), 1, '1440p screen with the standard 4K target: 1440p')
t.same(U.targetIndex(1080, 2), 1, '1080p screen: the smallest target')
t.same(U.targetIndex(2160, 1), 2, '4K screen with a 1440p target: 4K')
t.same(U.targetIndex(1600, 1), 2, 'more than 1440 lines: 4K')
t.same(U.targetIndex(1440, 1), 0, '1440p screen, 1440p target: nothing to change')
for (const big of [3, 4, 5, 6]) t.same(U.targetIndex(1440, big), 0, 'a larger target chosen in Professional mode is left alone (' + big + ')')
t.same([U.targetIndex(0, 2), U.targetIndex(undefined, 2), U.targetIndex(NaN, 2), U.targetIndex(1440, 0), U.targetIndex(1440, undefined)], [0, 0, 0, 0, 0], 'a screen or target not known: nothing is changed')

t.same([U.label(amd, '5'), U.label(amd, 'nope'), U.label(null, '5')], ['FSR 3.1 / 4', '', ''], 'a choice is named by its label')

t.done('push_upscale_test')
