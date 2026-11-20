// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Loads one of the interface's pure JavaScript libraries (qml/Veyra/*.js with `.pragma library`)
// into Node, so its rules can be run without Qt: the checks workflow runs every tests/js/*_test.js.
// Only files that use nothing of Qt can be loaded this way (no qsTr, no Qt.*).
'use strict'
const fs = require('fs')
const path = require('path')
const vm = require('vm')

exports.load = function (name) {
    const file = path.join(__dirname, '..', '..', 'qml', 'Veyra', name)
    const source = fs.readFileSync(file, 'utf8').replace(/^\s*\.pragma\s+library\s*$/m, '')
    const context = vm.createContext({})
    vm.runInContext(source, context, { filename: name })
    return context
}

let failed = 0, checked = 0
exports.check = function (condition, what) {
    ++checked
    if (!condition) { ++failed; console.error('FAILED: ' + what) }
}
exports.same = function (actual, expected, what) {
    exports.check(JSON.stringify(actual) === JSON.stringify(expected), what + ' (got ' + JSON.stringify(actual) + ', wanted ' + JSON.stringify(expected) + ')')
}
exports.done = function (name) {
    console.log(name + ': ' + checked + ' checks, ' + failed + ' failed')
    process.exit(failed ? 1 : 0)
}
