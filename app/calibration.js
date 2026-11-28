/* Pure measurement processing; the video uses 32 clockwise edge patches. */
function calibrationFit(baseline, patches, model) {
    if (!model || baseline.length !== 16 || patches.length !== 32) throw new Error('Incomplete calibration measurements.');
    var candidates = [],
        edges = [0, 0, 0, 0];
    for (var region = 0; region < 16; region++) {
        var responses = patches.map(function(p) {
            if (!p || p.length !== 16) throw new Error('Missing video measurements.');
            return Math.max(0, p[region] - baseline[region]);
        });
        var peak = Math.max.apply(Math, responses),
            best = responses.indexOf(peak);
        if (peak < 8) continue;
        // Reject regions responding equally around the screen (global histograms).
        var strong = responses.filter(function(v) {
            return v >= peak * .65;
        }).length;
        if (strong > 10) continue;
        var distant = responses.some(function(v, i) {
            var d = Math.min(Math.abs(i - best), 32 - Math.abs(i - best));
            return v >= peak * .65 && d > 6;
        });
        if (distant) continue;
        var sum = 0,
            weighted = 0;
        for (var offset = -3; offset <= 3; offset++) {
            var index = (best + offset + 32) % 32,
                weight = Math.max(0, responses[index] - peak * .35);
            weighted += offset * weight;
            sum += weight;
        }
        var position = ((best + .5 + weighted / sum) / 8 + 4) % 4;
        candidates.push({
            position: position,
            region: region,
            score: peak / strong
        });
    }
    // Nested LG regions can measure the same edge position. Keep the most
    // localized response rather than making duplicate interpolation anchors.
    candidates.sort(function(a, b) {
        return b.score - a.score || a.region - b.region;
    });
    var points = [];
    candidates.forEach(function(p) {
        if (points.some(function(q) {
                var d = Math.abs(p.position - q.position);
                return Math.min(d, 4 - d) < .06;
            })) return;
        points.push({
            position: p.position,
            region: p.region
        });
        edges[Math.floor(p.position)]++;
    });
    points.sort(function(a, b) {
        return a.position - b.position;
    });
    if (points.length < 4 || edges.some(function(n) {
            return !n;
        })) throw new Error('No reliable spatial response on every edge. Keep the video full-screen; this TV may not expose local histogram regions. Your saved profile is unchanged.');
    return {
        model: model,
        points: points
    };
}

function calibrationLuma(regions) {
    return regions.map(function(c) {
        return (c[0] + c[1] + c[2]) / 3;
    });
}

function calibrationAverage(samples) {
    if (samples.length < 3) throw new Error('Not enough fresh histogram samples. Keep the app visible and repeat calibration.');
    return samples[0].map(function(_, r) {
        return samples.reduce(function(sum, s) {
            return sum + s[r];
        }, 0) / samples.length;
    });
}

var responsePositions = [.10, .25, .375, .50, .625, .75, .90];
var responseChecks = [[0, .44], [1, .31], [2, .56], [3, .68], [4, 0]];
var responseRows = 4 * responsePositions.length + 5;

function unitResponse(values) {
    var sorted = values.slice().sort(function(a, b) { return a - b; });
    var baseline = sorted[4];
    return values.map(function(v) { return Math.max(0, v - baseline); });
}

function responseSimilarity(a, b) {
    var aa = 0, bb = 0, ab = 0;
    for (var i = 0; i < 16; i++) {
        aa += a[i] * a[i];
        bb += b[i] * b[i];
        ab += a[i] * b[i];
    }
    return aa && bb ? ab / Math.sqrt(aa * bb) : 0;
}

function learnEdgeResponse(baseline, patches, model) {
    if (!model || baseline.length !== 16 || patches.length !== responseRows + responseChecks.length) {
        throw new Error('Incomplete LED position measurements.');
    }
    var rows = patches.slice(0, responseRows).map(function(row) {
        var peak = Math.max.apply(Math, row.map(function(v, i) { return v - baseline[i]; }));
        var signal = unitResponse(row);
        var max = Math.max.apply(Math, signal);
        if (peak < 8 || max < 8) throw new Error('This TV did not provide a local response at every test position.');
        return signal.map(function(v) { return Math.round(1000 * v / max); });
    });
    var candidates = [];
    var width = responsePositions.length;
    for (var side = 0; side < 4; side++) {
        var segment = 0;
        for (var n = 0; n < 33; n++) {
            var along = (20 + 5 * n) / 200;
            if (along > responsePositions[segment + 1]) segment++;
            var a = rows[side * width + segment], b = rows[side * width + segment + 1];
            var weight = (along - responsePositions[segment]) / (responsePositions[segment + 1] - responsePositions[segment]);
            var mix = a.map(function(v, r) { return v * (1 - weight) + b[r] * weight; });
            candidates.push({side: side, along: along, row: mix});
        }
    }
    rows.slice(4 * width).forEach(function(row) { candidates.push({side: 4, along: 0, row: row}); });
    var verified = 0;
    responseChecks.forEach(function(test, i) {
        var signal = unitResponse(patches[responseRows + i]);
        var best = null, score = 0;
        candidates.forEach(function(candidate) {
            var similarity = responseSimilarity(signal, candidate.row);
            if (similarity > score) {
                best = candidate;
                score = similarity;
            }
        });
        if (best && score >= .88 && best.side === test[0] && Math.abs(best.along - test[1]) <= .16) verified++;
    });
    if (verified < 4) {
        throw new Error('The independent test fields could not be localized reliably. The saved profile is unchanged.');
    }
    return {profile: 'measured', responseModel: model, response: rows, verified: verified};
}
