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
        .rate-700C { background: #ff6b35; color: #000; }
        .rate-1200 { background: #4a9eff; color: #000; }
        .rate-1300 { background: #00d4ff; color: #000; }
        .rate-1400 { background: #22c55e; color: #000; }
        .rate-1600 { background: #84cc16; color: #000; }
        .rate-2400 { background: #00ff88; color: #000; }
        .rate-3200 { background: #a855f7; color: #000; }
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
                    
                    <!-- Info Section -->
                    <div style="margin-top: 16px; padding: 12px; background: #0a0a12; border-radius: 3px; font-size: 11px; color: #6a7080;">
                        <strong style="color: #00d4ff;">Codec2 Specifications:</strong><br>
                        • Open Source (LGPL) by VK5DGR<br>
                        • Rates: 700C to 3200 bps<br>
                        • 700C optimized for HF radio<br>
                        • Audio: 8000 Hz, 16-bit signed PCM<br>
                        <a href="https://github.com/drowe67/codec2" target="_blank" style="color: #4a9eff;">github.com/drowe67/codec2</a>
                    </div>
                </div>
                
                <!-- Audio Panels -->
                <div>
                    <div class="audio-panel">
                        <div class="audio-card">
                            <h3>Input Audio <span class="rate-badge" id="codec2-input-badge">Original</span></h3>
                            <canvas class="audio-viz" id="codec2-input-waveform"></canvas>
                            <div class="audio-player">
                                <button class="btn btn-play-input" onclick="playCodec2Input()">▶ Play</button>
                                <button class="btn btn-secondary" onclick="stopCodec2Input()">■ Stop</button>
                                <span id="codec2-input-duration" style="margin-left: auto; font-size: 11px; color: #6a7080;">--:--</span>
                            </div>
                        </div>
                    </div>
                    
                    <div class="audio-panel">
                        <div class="audio-card">
                            <h3>Output Audio <span class="rate-badge rate-1300" id="codec2-output-badge">1300 bps</span></h3>
                            <canvas class="audio-viz" id="codec2-output-waveform"></canvas>
                            <div class="audio-player">
                                <button class="btn btn-play-output" onclick="playCodec2Output()">▶ Play</button>
                                <button class="btn btn-secondary" onclick="stopCodec2Output()">■ Stop</button>
                                <span id="codec2-output-duration" style="margin-left: auto; font-size: 11px; color: #6a7080;">--:--</span>
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
let codec2AudioCtxIn = null;
let codec2AudioCtxOut = null;
let codec2InputBuffer = null;
let codec2OutputBuffer = null;
let codec2InputSource = null;
let codec2OutputSource = null;

function updateCodec2RateBadge() {
    const rate = document.getElementById('codec2-rate').value;
    const badge = document.getElementById('codec2-output-badge');
    badge.textContent = rate + ' bps';
    badge.className = 'rate-badge rate-' + rate;
}

async function onCodec2FileChange() {
    const filename = document.getElementById('codec2-file').value;
    const status = document.getElementById('codec2-status');
    status.className = 'codec2-status';
    status.textContent = 'Loading ' + filename + '...';
    
    try {
        // Use melpe-audio endpoint since they share the same audio directory
        const response = await fetch('/melpe-audio?file=' + encodeURIComponent(filename));
        if (!response.ok) throw new Error('File not found');
        
        const arrayBuffer = await response.arrayBuffer();
        const pcmData = new Int16Array(arrayBuffer);
        
        if (!codec2AudioCtxIn) {
            codec2AudioCtxIn = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 8000 });
        }
        
        const floatData = new Float32Array(pcmData.length);
        for (let i = 0; i < pcmData.length; i++) {
            floatData[i] = pcmData[i] / 32768.0;
        }
        
        codec2InputBuffer = codec2AudioCtxIn.createBuffer(1, floatData.length, 8000);
        codec2InputBuffer.getChannelData(0).set(floatData);
        
        const duration = floatData.length / 8000;
        document.getElementById('codec2-input-duration').textContent = 
            Math.floor(duration / 60) + ':' + Math.floor(duration % 60).toString().padStart(2, '0');
        
        drawCodec2Waveform('codec2-input-waveform', floatData);
        
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

async function runCodec2Vocoder() {
    const filename = document.getElementById('codec2-file').value;
    const rate = document.getElementById('codec2-rate').value;
    const status = document.getElementById('codec2-status');
    const stats = document.getElementById('codec2-stats');
    
    status.className = 'codec2-status';
    status.textContent = 'Running Codec2 ' + rate + ' bps loopback test...';
    
    try {
        const response = await fetch('/api/codec2/loopback?rate=' + rate + '&file=' + encodeURIComponent(filename));
        const data = await response.json();
        
        if (data.success) {
            status.className = 'codec2-status success';
            status.textContent = 'Loopback complete!';
            stats.innerHTML = 
                'Mode: ' + data.mode + '<br>' +
                'Frames: ' + data.frames + '<br>' +
                'Duration: ' + data.duration.toFixed(2) + ' sec<br>' +
                'Compression: ' + data.compression_ratio.toFixed(1) + ':1';
            
            // Load output audio
            await loadCodec2Output(data.output_file, rate);
        } else {
            status.className = 'codec2-status error';
            status.textContent = 'Error: ' + data.error;
        }
    } catch (e) {
        status.className = 'codec2-status error';
        status.textContent = 'Error: ' + e.message;
    }
}

async function loadCodec2Output(filename, rate) {
    try {
        const response = await fetch('/api/codec2/output?file=' + encodeURIComponent(filename));
        if (!response.ok) throw new Error('Output file not found');
        
        const arrayBuffer = await response.arrayBuffer();
        const pcmData = new Int16Array(arrayBuffer);
        
        if (!codec2AudioCtxOut) {
            codec2AudioCtxOut = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 8000 });
        }
        
        const floatData = new Float32Array(pcmData.length);
        for (let i = 0; i < pcmData.length; i++) {
            floatData[i] = pcmData[i] / 32768.0;
        }
        
        codec2OutputBuffer = codec2AudioCtxOut.createBuffer(1, floatData.length, 8000);
        codec2OutputBuffer.getChannelData(0).set(floatData);
        
        const duration = floatData.length / 8000;
        document.getElementById('codec2-output-duration').textContent = 
            Math.floor(duration / 60) + ':' + Math.floor(duration % 60).toString().padStart(2, '0');
        
        drawCodec2Waveform('codec2-output-waveform', floatData);
        
        const badge = document.getElementById('codec2-output-badge');
        badge.className = 'rate-badge rate-' + rate;
        badge.textContent = rate + ' bps';
    } catch (e) {
        console.error('Error loading output:', e);
    }
}

function playCodec2Input() {
    if (!codec2InputBuffer || !codec2AudioCtxIn) return;
    stopCodec2Input();
    codec2InputSource = codec2AudioCtxIn.createBufferSource();
    codec2InputSource.buffer = codec2InputBuffer;
    codec2InputSource.connect(codec2AudioCtxIn.destination);
    codec2InputSource.start();
}

function stopCodec2Input() {
    if (codec2InputSource) {
        try { codec2InputSource.stop(); } catch (e) {}
        codec2InputSource = null;
    }
}

function playCodec2Output() {
    if (!codec2OutputBuffer || !codec2AudioCtxOut) return;
    stopCodec2Output();
    codec2OutputSource = codec2AudioCtxOut.createBufferSource();
    codec2OutputSource.buffer = codec2OutputBuffer;
    codec2OutputSource.connect(codec2AudioCtxOut.destination);
    codec2OutputSource.start();
}

function stopCodec2Output() {
    if (codec2OutputSource) {
        try { codec2OutputSource.stop(); } catch (e) {}
        codec2OutputSource = null;
    }
}

// Initialize on tab switch
function initCodec2Tab() {
    onCodec2FileChange();
    updateCodec2RateBadge();
}
)JS";

} // namespace test_gui
