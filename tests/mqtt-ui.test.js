'use strict';

const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const elements = new Map();
const callbacks = [];
let refresh, restoreFinished, restores = 0;
const pending = ['overview status request'];
const context = {
    document: {
        getElementById(id) {
            if (!elements.has(id)) {
                elements.set(id, {style: {}, setAttribute() {}, addEventListener() {}});
            }
            return elements.get(id);
        }
    },
    window: {},
    pending,
    versionCompatible: true,
    repairRoot(done) { restores++; restoreFinished = done; },
    clearActionError() {},
    setupFormEditors() {},
    call(method, callback) {
        assert.strictEqual(method, 'mqttStatus');
        callbacks.push(callback);
    },
    setInterval(callback) {
        refresh = callback;
    }
};

vm.runInNewContext(fs.readFileSync('app/mqtt-ui.js', 'utf8'), context);
assert.strictEqual(callbacks.length, 1, 'Overview traffic must not block MQTT status');
refresh();
assert.strictEqual(callbacks.length, 1, 'Do not overlap two MQTT requests');

const status = {returnValue: true, enabled: true, sourceMode: 'Auto', connected: false};
callbacks.shift()(status);
assert.strictEqual(elements.get('mqttStatus').textContent, 'MQTT: Connecting...');

refresh();
assert.strictEqual(callbacks.length, 1);
callbacks.shift()({...status, connected: true, topic: 'hyperdrm/hdr-test'});
assert.strictEqual(elements.get('mqttStatus').textContent, 'MQTT: Connected: hyperdrm/hdr-test');
refresh();
callbacks.shift()({returnValue: true, rootRequired: true});
assert.strictEqual(restores, 0, 'A fresh setup must not request elevation automatically');
assert.strictEqual(elements.get('mqttSave').disabled, true);
assert.match(elements.get('mqttStatus').textContent, /needs root access/);

refresh();
const priorSetup = {returnValue: true, rootRequired: true, restoreAllowed: true};
callbacks.shift()(priorSetup);
assert.strictEqual(restores, 1);
restoreFinished(false);
refresh();
callbacks.shift()(priorSetup);
assert.strictEqual(restores, 1, 'Denied automatic elevation must not be retried in a loop');

elements.get('mqttRestoreRoot').onclick();
assert.strictEqual(restores, 2);
restoreFinished(true);
callbacks.shift()({...status, connected: true, topic: 'hyperdrm/hdr-test'});
assert.strictEqual(elements.get('mqttRestoreRoot').style.display, 'none');
assert.strictEqual(elements.get('mqttSave').disabled, false);
assert.strictEqual(elements.get('mqttStatus').textContent, 'MQTT: Connected: hyperdrm/hdr-test');

console.log('PASS: MQTT polling, prior-setup recovery, no retry loop and manual recovery');
