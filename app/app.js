'use strict';
var pending = [];
var actionError = '';
var versionCompatible = typeof HYPERDRM_APP_VERSION === 'undefined';

function connectionTestBusy(test) {
    return !!test && (typeof test.busy === 'boolean' ? test.busy :
        ['Connecting', 'Sending request', 'Checking authentication', 'Reading LED layout'].indexOf(test.state) >= 0);
}

function checkServiceVersion(result) {
    if (typeof HYPERDRM_APP_VERSION === 'undefined' || result.returnValue === false) return;
    versionCompatible = result.version === HYPERDRM_APP_VERSION;
    var warning = document.getElementById('versionWarning');
    warning.style.display = versionCompatible ? 'none' : 'block';
    document.getElementById('reloadService').style.display = versionCompatible ? 'none' : 'inline-block';
    warning.textContent = versionCompatible ? '' : 'App ' + HYPERDRM_APP_VERSION + ' / service ' + (result.version || 'unknown') + '. The installed service does not match this app. Finish the update, then select Reload service before changing settings. Reloading briefly stops ambient lighting.';
    ['start', 'autostart', 'handover', 'saveTarget', 'testConnection', 'calibrate', 'validateCalibration', 'saveProfile'].forEach(function(id) {
        var button = document.getElementById(id);
        if (!versionCompatible) {
            button.versionBlocked = true;
            button.disabled = true;
        } else if (button.versionBlocked) {
            button.versionBlocked = false;
            button.disabled = id === 'handover' ? !result.piccapInstalled : id === 'testConnection' && connectionTestBusy(result.connectionTest);
        }
    });
}

function showActionError(message) {
    actionError = message || '';
    document.getElementById('error').textContent = actionError;
}

function clearActionError() {
    showActionError('');
}
var targetDirty = false,
    receiver = 'Hyperion',
    output = 'json';
var profileDirty = false,
    profile = 'auto',
    geometry = 'rim',
    advancedProfile = false,
    responseCalibrationAvailable = false,
    pendingResponse = null;
var profileNames = {
    auto: 'Auto',
    g4: 'LG G4 (measured)',
    average: 'Average (uniform)',
    custom: 'Custom model',
    'g4-overlap': 'LG G4 (experimental edge correction)',
    measured: 'Measured LED positions'
};
var geometryNames = {
    rim: 'Perimeter',
    rectangles: 'Region rectangles'
};

function showProfile() {
    document.getElementById('profile').textContent = 'Profile: ' + profileNames[profile];
    document.getElementById('geometry').textContent = 'Geometry: ' + geometryNames[geometry];
    document.getElementById('geometry').disabled = profile !== 'custom';
    var visible = profile === 'custom' && advancedProfile;
    document.getElementById('customEditor').style.display = visible ? 'block' : 'none';
    document.getElementById('customProfile').disabled = !visible;
    document.getElementById('editProfile').disabled = profile !== 'custom';
    document.getElementById('editProfile').textContent = visible ? 'Hide profile JSON' : 'Edit profile JSON';
}

function call(method, done, data) {
    if (!versionCompatible && ['start', 'autostart', 'handover', 'settings', 'blackBarDetection', 'testConnection', 'calibration', 'calibrationSession', 'calibrationDraft', 'mqttSettings', 'sourceMode'].indexOf(method) >= 0 && !(method === 'calibrationSession' && data && data.enabled === false)) {
        done({
            returnValue: false,
            errorText: 'App and service versions do not match. Finish the update first.'
        });
        return;
    }
    if (['start', 'stop', 'autostart', 'handover', 'settings', 'blackBarDetection', 'testConnection', 'calibration'].indexOf(method) >= 0) clearActionError();
    callUri('luna://org.webosbrew.histogramhyperion.service/' + method, function(result) {
        if (method !== 'status' && result.returnValue === false) showActionError(result.errorText || 'The action failed.');
        done(result);
    }, data);
}

function callUri(uri, done, data) {
    if (typeof PalmServiceBridge === 'undefined') {
        done({
            returnValue: false,
            errorText: 'This app requires an LG TV with the native service installed.'
        });
        return;
    }
    var bridge = new PalmServiceBridge(),
        timer, finished = false;

    function finish(value) {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        var i = pending.indexOf(bridge);
        if (i >= 0) pending.splice(i, 1);
        done(value);
    }
    bridge.onservicecallback = function(raw) {
        try {
            finish(JSON.parse(raw));
        } catch (e) {
            finish({
                returnValue: false,
                errorText: 'Invalid service response'
            });
        }
    };
    pending.push(bridge);
    timer = setTimeout(function() {
        bridge.cancel();
        finish({
            returnValue: false,
            errorText: 'The service is not responding. Check the installation and service log.'
        });
    }, uri.indexOf('hbchannel') >= 0 ? 30000 : 10000);
    bridge.call(uri, JSON.stringify(data || {}));
}

function display(result) {
    var blackBars = document.getElementById('blackBarDetection');
    if (blackBars) {
        var supported = result.blackBarDetectionWritable === true && typeof result.blackBarDetection === 'boolean';
        blackBars.disabled = blackBars.changing || !supported ||
            (typeof HYPERDRM_APP_VERSION !== 'undefined' && result.version !== HYPERDRM_APP_VERSION);
        if (supported) {
            blackBars.setAttribute('aria-checked', String(result.blackBarDetection));
            blackBars.textContent = 'Black bar detection: ' + (result.blackBarDetection ? 'On' : 'Off');
        }
    }
    var responseButton = document.getElementById('calibrateOverlap');
    if (responseButton && typeof result.responseCalibrationSupported === 'boolean') {
        responseButton.style.display = result.responseCalibrationSupported ? 'inline-block' : 'none';
        responseCalibrationAvailable = result.responseCalibrationAvailable === true;
    }
    if (typeof result.uid === 'number') {
        document.getElementById('grantRoot').style.display = result.uid === 0 ? 'none' : 'inline-block';
        document.getElementById('rootHint').style.display = result.uid === 0 ? 'none' : 'block';
    }
    if (result.calibrationProfile && !profileDirty) {
        profile = result.calibrationProfile;
        geometry = result.customGeometry === 'rectangles' ? 'rectangles' : 'rim';
        showProfile();
        var stored = {
            model: result.customModel || result.tvModel || 'TV-MODEL',
            points: result.customPoints && result.customPoints.length ? result.customPoints : []
        };
        if (result.customGeometry === 'rectangles') stored.geometry = 'rectangles';
        else if (result.customRectangles && result.customRectangles.length) stored.geometry = 'rim';
        if (result.customRectangles && result.customRectangles.length) stored.rectangles = result.customRectangles;
        document.getElementById('customProfile').value = JSON.stringify(stored, null, 2);
    }
    if (result.effectiveCalibration) document.getElementById('profileStatus').textContent = 'TV: ' + (result.tvModel || 'Unknown') + ' · Active profile: ' + profileNames[result.effectiveCalibration] + (result.effectiveCalibration === 'custom' ? ' · ' + geometryNames[result.spatialGeometry === 'rectangles' ? 'rectangles' : 'rim'] : '') + (result.effectiveCalibration === 'average' ? ' · Spatial calibration needed for this model' : '');

    if (result.host && !targetDirty) {
        document.getElementById('host').value = result.host;
        document.getElementById('port').value = result.port;
        document.getElementById('piccapPort').value = result.piccapPort || 19400;
        document.getElementById('captureInterval').value = result.captureIntervalMs || 50;
        document.getElementById('blackCutoff').value = typeof result.blackCutoff === 'number' ? result.blackCutoff : 16;
        receiver = result.receiver;
        document.getElementById('receiver').textContent = 'Receiver: ' + receiver;
        output = result.output || 'json';
        document.getElementById('output').disabled = receiver !== 'HyperHDR';
        document.getElementById('output').textContent = 'Output: ' + (output === 'flatbuffer' ? 'FlatBuffer, all instances' : 'JSON, one instance');
    }
    document.getElementById('token').placeholder = result.tokenSet ? 'Saved token (leave blank to keep)' : 'No token saved';
    if (result.endpoint) document.getElementById('endpoint').textContent = result.receiver + ' · ' + result.endpoint + ' · Priority 149 · ' + (result.captureIntervalMs || 50) + ' ms capture interval';
    var timingLabel = document.getElementById('captureTiming');
    if (timingLabel) {
        var timing = result.timing;
        var cadence = timing && timing.sampleIntervalMs > 0
            ? 'Histogram cadence: ' + timing.sampleIntervalMs.toFixed(1) + ' ms (' +
                (1000 / timing.sampleIntervalMs).toFixed(1) + '/s). ' : '';
        timingLabel.textContent = timing && timing.totalMs >= 0
            ? cadence + 'Sample to receiver confirmation: ' + timing.totalMs + ' ms (capture and render ' +
                timing.captureMs + ' ms, queue and send ' + timing.queueMs + ' ms, confirmation ' +
                timing.ackMs + ' ms).'
            : cadence;
    }
    var t = result.connectionTest;
    if (t) document.getElementById('testResult').textContent = 'Connection test' + (t.transport ? ' (' + t.transport + ')' : '') + ': ' + t.state + (t.state === 'Passed' ? ' · ' + t.ledCount + ' LEDs · ' + t.elapsedMs + ' ms' : '') + (t.error ? ' · ' + t.error : '');
    document.getElementById('testConnection').disabled = connectionTestBusy(t);
    if (result.version) document.getElementById('diagnostics').textContent = 'Version ' + result.version + ' · ' + (result.uid === 0 ? 'Root service' : 'Service needs root elevation') + ' · ' + result.captureState + ' · Playback: ' + result.playbackProtection + (result.inputPreview ? ' · Recent input preview: ' + result.inputPreview : '') + ' · Capture recoveries: ' + result.captureRecoveries + ' (last: ' + result.lastRecoveryMs + ' ms)' + (result.lastOutputAgeMs >= 0 ? ' · Last histogram output: ' + result.lastOutputAgeMs + ' ms ago' : '') + (result.retryScheduled ? ' · Retry in ' + result.retryInMs + ' ms · Attempts: ' + result.retryAttempts : '') + (result.piccapError ? ' · ' + result.piccapError : '') + (result.piccapCooldownMs ? ' · PicCap retry in ' + Math.ceil(result.piccapCooldownMs / 1000) + ' s' : '') + ' · Layout updates: ' + (result.layoutUpdates || 0) + (result.layoutError ? ' · Layout: ' + result.layoutError : '');
    var handover = document.getElementById('handover');
    handover.disabled = !result.piccapInstalled;
    handover.setAttribute('aria-checked', String(!!result.piccapHandover));
    handover.textContent = result.piccapInstalled ? 'Auto PicCap: ' + (result.piccapHandover ? 'On' : 'Off') : 'Auto PicCap: Not installed';
    if (typeof result.autostart === 'boolean') {
        var toggle = document.getElementById('autostart');
        toggle.setAttribute('aria-checked', String(result.autostart));
        toggle.textContent = 'Autostart: ' + (result.autostart ? 'On' : 'Off');
    }
    document.getElementById('error').textContent = actionError || result.errorText || result.error || '';
    document.getElementById('status').textContent = result.returnValue === false ? 'Service unavailable' : (result.receiverConnecting ? 'Connecting to receiver' : result.running ? 'Running' : 'Stopped') + ' · ' + (result.frames || 0) + ' acknowledged color updates';
    if (result.powerPaused) {
        document.getElementById('status').textContent = 'Paused while TV display is off';
        document.getElementById('regions').textContent = '';
        checkServiceVersion(result);
        return;
    }
    if (result.activeSource === 'PicCap') {
        document.getElementById('status').textContent = 'PicCap · ' + (result.running ? 'Running' : 'Switching capture source') + ' · ' + (result.piccapConnected ? 'Receiver connected' : 'Waiting for receiver');
        document.getElementById('regions').textContent = '';
        checkServiceVersion(result);
        return;
    }
    if (result.piccapHandover) document.getElementById('status').textContent += ' · HyperDRM · ' + result.playbackProtection;
    if (result.piccapHandover && !result.piccapTargetCompatible) document.getElementById('status').textContent += ' · Auto PicCap unavailable for remote IPv6 target';
    if (result.colorValid && result.effectiveCalibration === 'average') document.getElementById('status').textContent += ' · Average color output';
    if (result.colorValid) document.getElementById('status').textContent += result.spatialOutput ? ' · ' + result.ledCount + ' LEDs from 16 color regions' + (result.receiver === 'HyperHDR' ? ' · Spatial image output' : '') : ' · Output RGB: ' + result.outputColor.join(', ');
    else if (result.returnValue !== false) document.getElementById('status').textContent += ' · No current color output';
    if (result.output === 'flatbuffer') document.getElementById('status').textContent += result.flatbufferConnected ? ' · FlatBuffer connected, every LED instance' : ' · FlatBuffer connecting';
    else if (result.returnValue !== false) document.getElementById('status').textContent += ' · No current color output';
    if (typeof result.blackBarDetection === 'boolean' && result.running) document.getElementById('status').textContent += result.letterboxDetected ? ' · Top/bottom black bars detected' : ' · Full-screen capture';
    if (result.spatialInterpolation) document.getElementById('status').textContent += ' · Smooth color transitions';
    if (result.spatialGeometry === 'rectangles') document.getElementById('status').textContent += ' · Region rectangles (1/area)';
    if (result.holdingColors) document.getElementById('status').textContent += ' · Holding last valid colors (missing data)';
    if (result.sourceFade) document.getElementById('status').textContent += ' · Blending capture sources';
    if (result.retryScheduled) document.getElementById('status').textContent += ' · Retrying receiver or capture startup';
    if (result.retryPaused) document.getElementById('status').textContent += ' · Automatic retry paused until Start';
    var box = document.getElementById('regions');
    box.textContent = '';
    (result.regions || []).forEach(function(rgb, index) {
        var e = document.createElement('div');
        e.className = 'swatch';
        e.style.backgroundColor = 'rgb(' + rgb.join(',') + ')';
        e.title = 'Histogram ' + index;
        box.appendChild(e);
    });
    checkServiceVersion(result);
}
document.getElementById('reloadService').onclick = function() {
    var button = this;
    button.disabled = true;
    clearActionError();
    call('restartService', function(result) {
        if (result.returnValue === false) {
            button.disabled = false;
            showActionError(result.errorText || 'Reinstall the current package, then restart its service.');
            return;
        }
        setTimeout(function() {
            button.disabled = false;
            call('status', display);
        }, 1500);
    });
};

document.getElementById('start').onclick = function() {
    call('start', function(r) {
        if (r.returnValue === false) display(r);
        else call('status', display);
    });
};
document.getElementById('stop').onclick = function() {
    call('stop', function() {
        call('status', display);
    });
};
document.getElementById('refresh').onclick = function() {
    call('status', display);
};
call('status', display);
setInterval(function() {
    if (!pending.length) call('status', display);
}, 3000);
document.getElementById('start').focus();

var repairingRoot = false,
    rootWaiters = [];

function repairRoot(done) {
    clearActionError();
    rootWaiters.push(done);
    if (repairingRoot) return;
    repairingRoot = true;
    document.getElementById('grantRoot').disabled = true;
    showActionError('Granting root access to HyperDRM...');

    function finish(error) {
        repairingRoot = false;
        document.getElementById('grantRoot').disabled = false;
        if (error) showActionError(error);
        var waiters = rootWaiters;
        rootWaiters = [];
        waiters.forEach(function(callback) {
            callback(!error);
        });
    }
    callUri('luna://org.webosbrew.hbchannel.service/elevateService', function(r) {
        if (r.returnValue === false) {
            finish('Homebrew Channel could not grant root access: ' + (r.errorText || 'Check Homebrew Channel installation.'));
            return;
        }
        call('restartService', function(r) {
            if (r.returnValue === false) {
                finish(r.errorText || 'Could not restart the HyperDRM service.');
                return;
            }
            var attempts = 0;

            function check() {
                call('status', function(s) {
                    if (s.uid === 0) {
                        display(s);
                        finish();
                    } else if (++attempts < 12) setTimeout(check, 1000);
                    else finish('The service is still missing root access. Elevate HyperDRM in Homebrew Channel and reopen the app.');
                });
            }
            setTimeout(check, 1500);
        });
    }, {
        id: 'org.webosbrew.histogramhyperion.service'
    });
}
document.getElementById('grantRoot').onclick = function() {
    repairRoot(function(ok) {
        if (ok) call('status', display);
    });
};
document.getElementById('autostart').onclick = function() {
    var button = this,
        enabled = button.getAttribute('aria-checked') !== 'true';
    button.disabled = true;

    function save() {
        call('autostart', function(r) {
            if (r.rootRequired) {
                repairRoot(function(ok) {
                    if (ok) save();
                    else button.disabled = false;
                });
                return;
            }
            button.disabled = false;
            if (r.returnValue === false) showActionError(r.errorText);
            else call('status', display);
        }, {
            enabled: enabled
        });
    }
    save();
};

document.getElementById('handover').onclick = function() {
    var button = this;
    button.disabled = true;
    call('handover', function(r) {
        button.disabled = false;
        if (r.returnValue === false) showActionError(r.errorText);
        else call('status', display);
    }, {
        enabled: button.getAttribute('aria-checked') !== 'true'
    });
};
var blackBarsButton = document.getElementById('blackBarDetection');
if (blackBarsButton) blackBarsButton.onclick = function() {
    if (this.disabled) return;
    var button = this;
    button.changing = button.disabled = true;
    call('blackBarDetection', function() {
        button.changing = false;
        call('status', display);
    }, { enabled: button.getAttribute('aria-checked') !== 'true' });
};
['host', 'port', 'piccapPort', 'captureInterval', 'blackCutoff', 'token', 'clearToken'].forEach(function(id) {
    document.getElementById(id).addEventListener('input', function() {
        targetDirty = true;
    });
});
document.getElementById('receiver').onclick = function() {
    receiver = receiver === 'Hyperion' ? 'HyperHDR' : 'Hyperion';
    this.textContent = 'Receiver: ' + receiver;
    var picker = document.getElementById('output');
    picker.disabled = receiver !== 'HyperHDR';
    if (picker.disabled) {
        output = 'json';
        picker.textContent = 'Output: JSON, one instance';
    }
    targetDirty = true;
};
document.getElementById('output').onclick = function() {
    if (receiver !== 'HyperHDR') return;
    output = output === 'flatbuffer' ? 'json' : 'flatbuffer';
    this.textContent = 'Output: ' + (output === 'flatbuffer' ? 'FlatBuffer, all instances' : 'JSON, one instance');
    targetDirty = true;
};
document.getElementById('saveTarget').onclick = function() {
    var port = Number(document.getElementById('port').value),
        button = this;
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
        showActionError('JSON port must be an integer from 1 to 65535.');
        return;
    }
    var flatPort = Number(document.getElementById('piccapPort').value);
    if (!Number.isInteger(flatPort) || flatPort < 1 || flatPort > 65535) {
        showActionError('PicCap FlatBuffer port must be an integer from 1 to 65535.');
        return;
    }
    var interval = Number(document.getElementById('captureInterval').value);
    if (!Number.isInteger(interval) || interval < 25 || interval > 1000 || interval % 25) {
        showActionError('Capture interval must be 25–1000 ms in steps of 25.');
        return;
    }
    var cutoffInput = document.getElementById('blackCutoff').value;
    var cutoff = Number(cutoffInput);
    if (cutoffInput === '' || !Number.isInteger(cutoff) || cutoff < 0 || cutoff > 64) {
        showActionError('Black cutoff must be an integer from 0 to 64.');
        return;
    }
    var settings = {
        captureIntervalMs: interval,
        blackCutoff: cutoff,
        piccapPort: flatPort,
        host: document.getElementById('host').value.trim(),
        port: port,
        receiver: receiver,
        output: output,
        clearToken: document.getElementById('clearToken').checked
    };
    var token = document.getElementById('token').value.trim();
    if (token) settings.token = token;
    button.disabled = true;
    call('settings', function(r) {
        button.disabled = false;
        if (r.returnValue === false) {
            showActionError(r.errorText);
            return;
        }
        targetDirty = false;
        document.getElementById('token').value = '';
        document.getElementById('clearToken').checked = false;
        call('status', display);
    }, settings);
};
document.getElementById('testConnection').onclick = function() {
    if (targetDirty) {
        showActionError('Save receiver settings before testing the connection.');
        return;
    }
    this.disabled = true;
    call('testConnection', function(r) {
        if (r.returnValue === false) showActionError(r.errorText);
        call('status', display);
    });
};
document.getElementById('profile').onclick = function() {
    var keys = Object.keys(profileNames).filter(function(key) {
        return key !== 'measured' || responseCalibrationAvailable || pendingResponse;
    });
    profile = keys[(keys.indexOf(profile) + 1) % keys.length];
    profileDirty = true;
    showProfile();
};
document.getElementById('customProfile').addEventListener('input', function() {
    profileDirty = true;
});
document.getElementById('saveProfile').onclick = function() {
    var data = {
            profile: profile
        },
        button = this;
    if (profile === 'custom') {
        try {
            var parsed = JSON.parse(document.getElementById('customProfile').value);
            data.model = parsed.model;
            data.points = parsed.points;
            data.geometry = parsed.geometry === 'rectangles' ? 'rectangles' : parsed.geometry === 'rim' ? 'rim' : geometry;
            if (parsed.rectangles && parsed.rectangles.length) data.rectangles = parsed.rectangles;
        } catch (e) {
            showActionError('Custom profile must be valid JSON.');
            return;
        }
    } else if (profile === 'measured' && pendingResponse) {
        data.responseModel = pendingResponse.responseModel;
        data.response = pendingResponse.response;
    }
    button.disabled = true;
    call('calibration', function(r) {
        button.disabled = false;
        if (r.returnValue === false) {
            showActionError(r.errorText);
            return;
        }
        profileDirty = false;
        if (data.response) pendingResponse = null;
        call('status', display);
    }, data);
};
document.getElementById('editProfile').onclick = function() {
    advancedProfile = !advancedProfile;
    showProfile();
    if (advancedProfile) document.getElementById('customProfileField').focus();
};
document.getElementById('geometry').onclick = function() {
    geometry = geometry === 'rim' ? 'rectangles' : 'rim';
    profileDirty = true;
    var field = document.getElementById('customProfile'),
        stored = null;
    try {
        stored = JSON.parse(field.value);
    } catch (e) {
        stored = null;
    }
    if (stored && typeof stored === 'object') {
        stored.geometry = geometry;
        field.value = JSON.stringify(stored, null, 2);
    }
    showProfile();
};
setupRemoteNavigation(['overviewTab', 'receiverTab', 'calibrationTab', 'reloadService', 'grantRoot', 'start', 'stop', 'refresh', 'autostart', 'handover', 'sourceMode', 'calibrationShortcut', 'receiver', 'output', 'hostField', 'portField', 'piccapPortField', 'captureIntervalField', 'blackCutoffField', 'tokenField', 'clearToken', 'saveTarget', 'testConnection', 'blackBarDetection', 'mqttEnabled', 'mqttHostField', 'mqttPortField', 'mqttUserField', 'mqttPasswordField', 'mqttClearPassword', 'mqttSave', 'mqttRestoreRoot', 'calibrateOverlap', 'calibrate', 'validateCalibration', 'profile', 'saveProfile', 'editProfile', 'geometry', 'customProfileField', 'cancelCalibration'].map(function(id) {
    return document.getElementById(id);
}).filter(Boolean), document, window);
