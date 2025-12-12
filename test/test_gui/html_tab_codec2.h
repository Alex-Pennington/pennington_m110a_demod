#pragma once
/**
 * @file html_tab_codec2.h
 * @brief Codec2 Vocoder tab HTML/JS for M110A Test GUI
 * 
 * Codec2 by David Rowe VK5DGR - Open Source (LGPL)
 * https://github.com/drowe67/codec2
 */

namespace test_gui {

// ============================================================
// CODEC2 TAB CSS
// ============================================================
const char* HTML_CSS_CODEC2 = R"CSS(
        /* Codec2 Vocoder Styles */
        .codec2-container { display: grid; grid-template-columns: 350px 1fr; gap: 20px; }
        @media (max-width: 900px) { .codec2-container { grid-template-columns: 1fr; } }
        .codec2-header { margin-bottom: 20px; }
        .codec2-header h2 { color: #00d4ff; margin: 0 0 5px 0; }
        .codec2-header p { color: #6a7080; font-size: 12px; margin: 0; }
        .codec2-controls { background: #0f0f1a; border: 1px solid #1e1e2e; border-radius: 4px; padding: 16px; }
        .codec2-status { margin-top: 12px; padding: 10px; background: #0a0a12; border-radius: 3px; font-size: 12px; color: #6a7080; }
        .codec2-status.success { color: #00ff88; border-left: 3px solid #00ff88; }
        .codec2-status.error { color: #ff3a50; border-left: 3px solid #ff3a50; }
        .audio-panel { background: #0f0f1a; border: 1px solid #1e1e2e; border-radius: 4px; padding: 16px; margin-bottom: 16px; }
        .audio-card { margin-bottom: 16px; }
        .audio-card:last-child { margin-bottom: 0; }
        .audio-player { display: flex; align-items: center; gap: 10px; margin-top: 10px; }
        .audio-viz { width: 100%; height: 60px; background: #0a0a12; border: 1px solid #1e1e2e; border-radius: 3px; }
        .rate-badge { display: inline-block; padding: 2px 8px; border-radius: 3px; font-size: 10px; font-weight: bold; margin-left: 8px; }
        .rate-700 { background: #ff6b35; color: #000; }
        .rate-1200 { background: #4a9eff; color: #000; }
        .rate-1300 { background: #00d4ff; color: #000; }
        .rate-2400 { background: #00ff88; color: #000; }
        .rate-3200 { background: #a855f7; color: #000; }
        .record-section { margin-top: 16px; padding-top: 16px; border-top: 1px solid #1e1e2e; }
        .btn-record { background: #ff3a50; }
        .btn-record.recording { animation: pulse 1s infinite; }
        .record-timer { font-family: monospace; font-size: 14px; color: #ff3a50; margin-left: 10px; }
        .btn-play-input, .btn-play-output { background: #00d4ff; color: #000; }
        .btn-run-vocoder { background: #00ff88; color: #000; }
)CSS";

// ============================================================
// CODEC2 TAB HTML
// ============================================================
const char* HTML_TAB_CODEC2 = R"HTML(
        <!-- ============ CODEC2 VOCODER TAB ============ -->
        <div id="tab-codec2" class="tab-content">
            <div class="codec2-header">
                <h2>Codec2 Vocoder Test</h2>
                <p>Open Source Voice Codec by David Rowe VK5DGR (LGPL)</p>
            </div>
            
            <div class="codec2-container">
                <!-- Controls Panel -->
                <div class="codec2-controls">
                    <h3>Test Configuration</h3>
                    
                    <div class="form-group" style="margin-bottom: 12px;">
                        <label>Audio File</label>
                        <select id="codec2-file" onchange="onCodec2FileChange()">
                            <option value="OSR_us_000_0010_8k.raw">OSR Sample 0010 (Harvard Sentences)</option>
                            <option value="OSR_us_000_0011_8k.raw">OSR Sample 0011 (Harvard Sentences)</option>
                            <option value="OSR_us_000_0030_8k.raw">OSR Sample 0030 (Harvard Sentences)</option>
                            <option value="OSR_us_000_0031_8k.raw">OSR Sample 0031 (Harvard Sentences)</option>
                        </select>
                    </div>
                    
                    <div class="form-group" style="margin-bottom: 12px;">
                        <label>Bit Rate</label>
                        <select id="codec2-rate" onchange="updateCodec2RateBadge()">
                            <option value="3200">3200 bps (highest quality)</option>
                            <option value="2400">2400 bps</option>
                            <option value="1600">1600 bps</option>
                            <option value="1400">1400 bps</option>
                            <option value="1300" selected>1300 bps (default)</option>
                            <option value="1200">1200 bps</option>
                            <option value="700C">700C bps (best for HF)</option>
                        </select>
                    </div>
                    
                    <button class="btn btn-run-vocoder" onclick="runCodec2Vocoder()" style="width: 100%;">
                        ▶ Run Loopback Test
                    </button>
                    
                    <div class="codec2-status" id="codec2-status">
                        Ready. Select a file and bit rate, then click Run.
                    </div>
                    
                    <!-- Recording Section -->
                    <div class="record-section">
                        <h3>🎤 Record Custom Audio</h3>
                        <div class="form-row" style="margin-bottom: 10px;">
                            <button class="btn btn-record" id="btn-record-codec2" onclick="toggleCodec2Recording()">
                                ⏺ Start Recording
                            </button>
                            <span class="record-timer" id="record-timer-codec2">0:00</span>
                        </div>
                        <p style="font-size: 11px; color: #6a7080; margin-top: 8px;">
                            Audio: 8000 Hz, 16-bit mono PCM
                        </p>
                    </div>
                    
                    <div style="margin-top: 16px; padding: 12px; background: #0a0a12; border-radius: 4px;">
                        <h4 style="margin: 0 0 8px 0; color: #00d4ff; font-size: 12px;">About Codec2</h4>
                        <p style="font-size: 11px; color: #6a7080; margin: 0;">
                            Codec2 is an open-source speech codec designed for low bitrate 
                            voice over HF/VHF radio. The 700C mode is specifically optimized 
                            for HF channel conditions.
                        </p>
                        <p style="font-size: 10px; color: #4a5568; margin: 8px 0 0 0;">
                            <a href="https://github.com/drowe67/codec2" style="color: #4a9eff;">github.com/drowe67/codec2</a>
                        </p>
                    </div>
                </div>
                
                <!-- Results Panel -->
                <div>
                    <div class="audio-panel">
                        <h3>Audio Comparison</h3>
                        
                        <div class="audio-card">
                            <div style="display: flex; align-items: center;">
                                <strong>Input (Original)</strong>
                            </div>
                            <div class="audio-player">
                                <button class="btn btn-play-input btn-sm" onclick="playCodec2Input()">▶ Play</button>
                                <canvas id="codec2-viz-input" class="audio-viz"></canvas>
                            </div>
                        </div>
                        
                        <div class="audio-card">
                            <div style="display: flex; align-items: center;">
                                <strong>Output (After Codec2)</strong>
                                <span class="rate-badge rate-1300" id="codec2-rate-badge">1300 bps</span>
                            </div>
                            <div class="audio-player">
                                <button class="btn btn-play-output btn-sm" onclick="playCodec2Output()">▶ Play</button>
                                <canvas id="codec2-viz-output" class="audio-viz"></canvas>
                            </div>
                        </div>
                    </div>
                    
                    <div class="audio-panel">
                        <h3>Statistics</h3>
                        <div id="codec2-stats" style="font-family: monospace; font-size: 12px; color: #6a7080;">
                            Run a test to see statistics...
                        </div>
                    </div>
                </div>
            </div>
        </div>
)HTML";

// ============================================================
// CODEC2 TAB JAVASCRIPT
// ============================================================
const char* HTML_JS_CODEC2 = R"JS(
// Codec2 Vocoder Functions

let codec2AudioContext = null;
let codec2InputBuffer = null;

async function onCodec2FileChange() {
    const filename = document.getElementById('codec2-file').value;
    const status = document.getElementById('codec2-status');
    status.className = 'codec2-status';
    status.textContent = 'Loading ' + filename + '...';
    
    try {
        const response = await fetch('/melpe-audio?file=' + encodeURIComponent(filename));
        if (!response.ok) throw new Error('File not found');
        
        const arrayBuffer = await response.arrayBuffer();
        const pcmData = new Int16Array(arrayBuffer);
        
        if (!codec2AudioContext) {
            codec2AudioContext = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 8000 });
        }
        
        const floatData = new Float32Array(pcmData.length);
        for (let i = 0; i < pcmData.length; i++) {
            floatData[i] = pcmData[i] / 32768.0;
        }
        
        codec2InputBuffer = codec2AudioContext.createBuffer(1, floatData.length, 8000);
        codec2InputBuffer.getChannelData(0).set(floatData);
        
        const duration = floatData.length / 8000;
        
        drawCodec2Waveform('codec2-viz-input', floatData);
        
        status.className = 'codec2-status success';
        status.textContent = 'Loaded ' + filename + ' (' + duration.toFixed(1) + 's)';
    } catch (e) {
        status.className = 'codec2-status error';
        status.textContent = 'Error loading file: ' + e.message;
    }
}

function drawCodec2Waveform(canvasId, data) {
    const canvas = document.getElementById(canvasId);
    const ctx = canvas.getContext('2d');
    const width = canvas.width = canvas.offsetWidth;
    const height = canvas.height = canvas.offsetHeight;
    
    ctx.fillStyle = '#0a0a12';
    ctx.fillRect(0, 0, width, height);
    
    ctx.strokeStyle = '#00d4ff';
    ctx.lineWidth = 1;
    ctx.beginPath();
    
    const step = Math.ceil(data.length / width);
    const mid = height / 2;
    
    for (let i = 0; i < width; i++) {
        let min = 1.0, max = -1.0;
        for (let j = 0; j < step; j++) {
            const idx = i * step + j;
            if (idx < data.length) {
                if (data[idx] < min) min = data[idx];
                if (data[idx] > max) max = data[idx];
            }
        }
        const y1 = mid - min * mid;
        const y2 = mid - max * mid;
        ctx.moveTo(i, y1);
        ctx.lineTo(i, y2);
    }
    
    ctx.stroke();
}

let codec2OutputContext = null;
let codec2OutputBuffer = null;

async function loadCodec2OutputAudio(filename, rate) {
    try {
        const response = await fetch('/api/codec2/output?file=' + encodeURIComponent(filename));
        if (!response.ok) throw new Error('Output file not found');
        
        const arrayBuffer = await response.arrayBuffer();
        const pcmData = new Int16Array(arrayBuffer);
        
        if (!codec2OutputContext) {
            codec2OutputContext = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 8000 });
        }
        
        const floatData = new Float32Array(pcmData.length);
        for (let i = 0; i < pcmData.length; i++) {
            floatData[i] = pcmData[i] / 32768.0;
        }
        
        codec2OutputBuffer = codec2OutputContext.createBuffer(1, floatData.length, 8000);
        codec2OutputBuffer.getChannelData(0).set(floatData);
        
        drawCodec2Waveform('codec2-viz-output', floatData);
    } catch (e) {
        console.error('Error loading Codec2 output:', e);
    }
}

function updateCodec2RateBadge() {
    const rate = document.getElementById('codec2-rate').value;
    const badge = document.getElementById('codec2-rate-badge');
    badge.textContent = rate + ' bps';
    badge.className = 'rate-badge rate-' + rate.replace('C', '').substring(0, 4);
}

function runCodec2Vocoder() {
    const rate = document.getElementById('codec2-rate').value;
    const file = document.getElementById('codec2-file').value;
    const status = document.getElementById('codec2-status');
    const stats = document.getElementById('codec2-stats');
    
    status.className = 'codec2-status';
    status.textContent = 'Running Codec2 ' + rate + ' bps loopback test...';
    
    fetch('/api/codec2/loopback?rate=' + rate + '&file=' + encodeURIComponent(file))
        .then(r => r.json())
        .then(data => {
            if (data.success) {
                status.className = 'codec2-status success';
                status.textContent = 'Loopback complete!';
                stats.innerHTML = 
                    'Mode: ' + data.mode + '<br>' +
                    'Frames: ' + data.frames + '<br>' +
                    'Duration: ' + data.duration.toFixed(2) + ' sec<br>' +
                    'Compression: ' + data.compression_ratio.toFixed(1) + ':1';
                loadCodec2OutputAudio(data.output_file, rate);
            } else {
                status.className = 'codec2-status error';
                status.textContent = 'Error: ' + data.error;
            }
        })
        .catch(err => {
            status.className = 'codec2-status error';
            status.textContent = 'Error: ' + err.message;
        });
}

let codec2Recording = false;
let codec2RecordTimer = null;
let codec2RecordSeconds = 0;

function toggleCodec2Recording() {
    const btn = document.getElementById('btn-record-codec2');
    const timer = document.getElementById('record-timer-codec2');
    
    if (!codec2Recording) {
        codec2Recording = true;
        codec2RecordSeconds = 0;
        btn.classList.add('recording');
        btn.textContent = '⏹ Stop Recording';
        
        codec2RecordTimer = setInterval(() => {
            codec2RecordSeconds++;
            const mins = Math.floor(codec2RecordSeconds / 60);
            const secs = codec2RecordSeconds % 60;
            timer.textContent = mins + ':' + (secs < 10 ? '0' : '') + secs;
        }, 1000);
        
        // Start recording via API
        fetch('/api/codec2/record/start', { method: 'POST' });
    } else {
        codec2Recording = false;
        btn.classList.remove('recording');
        btn.textContent = '⏺ Start Recording';
        clearInterval(codec2RecordTimer);
        
        // Stop recording via API
        fetch('/api/codec2/record/stop', { method: 'POST' });
    }
}

let codec2InputSourceNode = null;
let codec2OutputSourceNode = null;

function playCodec2Input() {
    if (!codec2InputBuffer || !codec2AudioContext) return;
    stopCodec2Input();
    codec2InputSourceNode = codec2AudioContext.createBufferSource();
    codec2InputSourceNode.buffer = codec2InputBuffer;
    codec2InputSourceNode.connect(codec2AudioContext.destination);
    codec2InputSourceNode.start();
}

function stopCodec2Input() {
    if (codec2InputSourceNode) {
        try { codec2InputSourceNode.stop(); } catch (e) {}
        codec2InputSourceNode = null;
    }
}

function playCodec2Output() {
    if (!codec2OutputBuffer || !codec2OutputContext) return;
    stopCodec2Output();
    codec2OutputSourceNode = codec2OutputContext.createBufferSource();
    codec2OutputSourceNode.buffer = codec2OutputBuffer;
    codec2OutputSourceNode.connect(codec2OutputContext.destination);
    codec2OutputSourceNode.start();
}

function stopCodec2Output() {
    if (codec2OutputSourceNode) {
        try { codec2OutputSourceNode.stop(); } catch (e) {}
        codec2OutputSourceNode = null;
    }
}

async function loadCodec2Files() {
    try {
        const response = await fetch('/melpe-list-recordings');
        const data = await response.json();
        if (data.recordings && data.recordings.length > 0) {
            const select = document.getElementById('codec2-file');
            // Remove any existing custom recordings section
            const existingCustom = select.querySelectorAll('option[data-custom]');
            existingCustom.forEach(opt => opt.remove());
            
            let hasCustomSection = false;
            data.recordings.forEach(rec => {
                if (!hasCustomSection) {
                    const opt = document.createElement('option');
                    opt.disabled = true;
                    opt.setAttribute('data-custom', 'true');
                    opt.textContent = '── Custom Recordings ──';
                    select.appendChild(opt);
                    hasCustomSection = true;
                }
                const opt = document.createElement('option');
                opt.value = rec.filename;
                opt.setAttribute('data-custom', 'true');
                opt.textContent = rec.name + ' (' + rec.duration.toFixed(1) + 's)';
                select.appendChild(opt);
            });
        }
    } catch (e) {
        console.log('Could not load custom recordings:', e);
    }
    onCodec2FileChange();
}
)JS";

} // namespace test_gui
