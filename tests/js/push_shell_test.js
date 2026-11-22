// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The new design's shell (qml/Veyra/PushShell.js): its sides, where a connected capture card plays,
// and the folded blocks of Professional mode's All tab.
'use strict'
const t = require('./load.js')
const S = t.load('PushShell.js')

// The navigation.
t.check(S.navForcedSlim(1099) && !S.navForcedSlim(1100) && !S.navForcedSlim(1920), 'a narrow window shows the icons alone')
t.check(S.navForcedSlim(undefined) && S.navForcedSlim(NaN) && S.navForcedSlim(0), 'a width not known is narrow')
t.check(!S.navSlim(1600, {}) && !S.navSlim(1600, null) && !S.navSlim(1600, undefined), 'wide and nothing chosen: the names are shown')
t.check(S.navSlim(1600, { pushNavCollapsed: true }), 'folded with the button')
t.check(!S.navSlim(1600, { pushNavCollapsed: false }) && !S.navSlim(1600, { pushNavCollapsed: 'true' }) && !S.navSlim(1600, { pushNavCollapsed: 1 }), 'only a real yes folds it')
t.check(S.navSlim(900, { pushNavCollapsed: false }), 'a narrow window stays folded whatever was chosen')

// The settings side.
t.check(S.sideOffered('home', true) && S.sideOffered('pro', false) && S.sideOffered('pro', true), 'the home page with its three panes and Professional mode have one')
t.check(!S.sideOffered('home', false) && !S.sideOffered('home', undefined), 'the one-column home page has none')
for (const page of ['live', 'watch', 'exp', 'min', 'set', 'node', '', undefined])
    t.check(!S.sideOffered(page, true), 'no settings side on ' + page)
t.check(S.sideCollapsed({ pushSideCollapsed: true }) && !S.sideCollapsed({}) && !S.sideCollapsed(null) && !S.sideCollapsed({ pushSideCollapsed: 'yes' }), 'hidden only when chosen')

// Where a capture card connected from the home page plays.
t.check(S.staysHomeOnConnect(true, true, {}), 'the new home page shows the picture: it plays there')
t.check(S.staysHomeOnConnect(true, true, null) && S.staysHomeOnConnect(true, true, undefined), 'also with no preferences at all')
t.check(!S.staysHomeOnConnect(false, true, {}) && !S.staysHomeOnConnect(false, false, {}), 'Legacy has no picture on its home page: Professional mode, as always')
t.check(!S.staysHomeOnConnect(true, false, {}), 'a window too narrow for the three panes: Professional mode')
t.check(!S.staysHomeOnConnect(undefined, undefined, {}) && !S.staysHomeOnConnect('true', 'true', {}), 'anything not a plain yes: Professional mode')
t.check(!S.staysHomeOnConnect(true, true, { pushConnectToPro: true }), 'whoever asked for Professional mode gets it')
t.check(!S.staysHomeOnConnect(true, true, { pushAutoFullscreen: true }), 'full screen on connecting goes full screen in Professional mode, as before')
t.check(S.staysHomeOnConnect(true, true, { pushConnectToPro: false, pushAutoFullscreen: false }), 'both switched off: the home page')

// Full screen from the home page.
t.check(S.fullScreenLeavesHome('home', true, true), 'a picture playing on the new home page: full screen is the picture')
t.check(!S.fullScreenLeavesHome('home', true, false) && !S.fullScreenLeavesHome('home', true, undefined), 'nothing playing: the page goes full screen as it did')
t.check(!S.fullScreenLeavesHome('home', false, true), 'Legacy: as it did')
for (const page of ['pro', 'min', 'live', 'watch', 'exp', 'set', 'node'])
    t.check(!S.fullScreenLeavesHome(page, true, true), page + ' goes full screen itself')

// The folded blocks of the All tab.
t.same(S.foldedBlocks(''), [], 'nothing saved: nothing folded')
t.same(S.foldedBlocks(undefined), [], 'no preference: nothing folded')
t.same(S.foldedBlocks(null), [], 'null: nothing folded')
t.same(S.foldedBlocks('fg,display'), ['fg', 'display'], 'the saved list')
t.same(S.foldedBlocks('fg,fg,,nonsense,quality, color'), ['fg', 'quality'], 'doubles, empties and what is no block are dropped')
for (const id of S.kBlocks) t.check(S.blockOpen('', id), id + ' is open until folded')
t.check(!S.blockOpen('fg,display', 'fg') && S.blockOpen('fg,display', 'quality') && !S.blockOpen('fg,display', 'display'), 'open or folded by the list')
t.same(S.withBlock('', 'fg', false), 'fg', 'folding adds')
t.same(S.withBlock('fg', 'color', false), 'fg,color', 'in the order they were folded')
t.same(S.withBlock('fg,color', 'fg', true), 'color', 'opening removes')
t.same(S.withBlock('fg,color', 'fg', false), 'color,fg', 'folding what is folded keeps it once')
t.same(S.withBlock('fg', 'quality', true), 'fg', 'opening what is open changes nothing')
t.same(S.withBlock('fg', 'nonsense', false), 'fg', 'what is no block is never added')
t.same(S.withBlock('nonsense,fg', 'audio', false), 'fg,audio', 'and what was saved wrongly is cleaned on the way')

t.done('push_shell_test')
