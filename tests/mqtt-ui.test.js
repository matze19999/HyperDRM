'use strict';

const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const elements = new Map();
const callbacks = [];
let refresh;
const pending = ['overview status request'];
const context = {
    document: {
        getElementById(id) {
            if (!elements.has(id)) {
                elements.set(id, {setAttribute() {}, addEventListener() {}});
            }
            return elements.get(id);
        }
    },
    window: {},
    pending,
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
console.log('PASS: MQTT status updates while Overview status is pending');
