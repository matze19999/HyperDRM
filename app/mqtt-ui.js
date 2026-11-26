'use strict';
(function() {
    var host = document.getElementById('mqttHost');
    var port = document.getElementById('mqttPort');
    var username = document.getElementById('mqttUser');
    var password = document.getElementById('mqttPassword');
    var clear = document.getElementById('mqttClearPassword');
    var toggle = document.getElementById('mqttEnabled');
    var save = document.getElementById('mqttSave');
    var source = document.getElementById('sourceMode');
    var status = document.getElementById('mqttStatus');
    var modes = ['Auto', 'HyperDRM', 'PicCap'];
    var dirty = false, enabled = false, mode = 'Auto', busy = false, piccapAvailable = false;
    var refreshing = false, settingsRevision = 0;

    setupFormEditors(['mqttHost', 'mqttPort', 'mqttUser', 'mqttPassword'], document, window);
    [host, port, username, password, clear].forEach(function(field) {
        field.addEventListener('input', function() { dirty = true; });
    });

    function showEnabled() {
        toggle.setAttribute('aria-checked', String(enabled));
        toggle.textContent = 'MQTT: ' + (enabled ? 'On' : 'Off');
    }

    function refresh() {
        if (busy || refreshing) return;
        refreshing = true;
        var revision = settingsRevision;
        call('mqttStatus', function(result) {
            refreshing = false;
            if (revision !== settingsRevision) {
                refresh();
                return;
            }
            if (!result.returnValue) return;
            mode = result.sourceMode;
            piccapAvailable = result.piccapAvailable;
            source.textContent = 'Source mode: ' + mode;
            if (!dirty) {
                enabled = result.enabled;
                host.value = result.host;
                port.value = result.port;
                username.value = result.username;
                password.placeholder = result.passwordSet ? 'Saved password (blank keeps it)' : 'No password saved';
                showEnabled();
            }
            status.textContent = 'MQTT: ' + (result.connected ? 'Connected: ' + result.topic :
                result.enabled ? (result.error || 'Connecting...') : 'Off');
        });
    }

    toggle.onclick = function() {
        dirty = true;
        enabled = !enabled;
        showEnabled();
    };
    save.onclick = function() {
        var number = Number(port.value);
        if (!Number.isInteger(number) || number < 1 || number > 65535) {
            showActionError('Broker port must be an integer from 1 to 65535.');
            return;
        }
        var body = {enabled: enabled, host: host.value.trim(), port: number, username: username.value};
        if (clear.checked || password.value) body.password = clear.checked ? '' : password.value;
        settingsRevision++;
        busy = save.disabled = true;
        call('mqttSettings', function(result) {
            busy = save.disabled = false;
            if (!result.returnValue) return;
            dirty = false;
            password.value = '';
            clear.checked = false;
            refresh();
        }, body);
    };
    source.onclick = function() {
        var next = modes[(modes.indexOf(mode) + 1) % modes.length];
        if (next === 'PicCap' && !piccapAvailable) next = 'Auto';
        source.disabled = true;
        call('sourceMode', function(result) {
            source.disabled = false;
            if (result.returnValue) {
                mode = next;
                source.textContent = 'Source mode: ' + mode;
            }
        }, {mode: next});
    };
    setInterval(refresh, 3000);
    refresh();
}());
