#pragma once
/**
 * @file html_tab_melpe.h
 * @brief MELPe Vocoder tab HTML/JS for M110A Test GUI
 * 
 * NATO STANAG 4591 - Mixed-Excitation Linear Prediction Enhanced
 */

namespace test_gui {

// ============================================================
// MELPE TAB CSS
// ============================================================
const char* HTML_CSS_MELPE = R"CSS(
        /* MELPe Vocoder Styles */
        .melpe-container { display: grid; grid-template-columns: 350px 1fr; gap: 20px; }
        @media (max-width: 900px) { .melpe-container { grid-template-columns: 1fr; } }
        .melpe-header { margin-bottom: 20px; }
        .melpe-header h2 { color: #00d4ff; margin: 0 0 5px 0; }
        .melpe-header p { color: #6a7080; font-size: 12px; margin: 0; }
        .melpe-controls { background: #0f0f1a; border: 1px solid #1e1e2e; border-radius: 4px; padding: 16px; }
        .melpe-status { margin-top: 12px; padding: 10px; background: #0a0a12; border-radius: 3px; font-size: 12px; color: #6a7080; }
        .melpe-status.success { color: #00ff88; border-left: 3px solid #00ff88; }
        .melpe-status.error { color: #ff3a50; border-left: 3px solid #ff3a50; }
        .audio-panel { background: #0f0f1a; border: 1px solid #1e1e2e; border-radius: 4px; padding: 16px; margin-bottom: 16px; }
        .audio-card { margin-bottom: 16px; }
        .audio-card:last-child { margin-bottom: 0; }
        .audio-player { display: flex; align-items: center; gap: 10px; margin-top: 10px; }
        .audio-viz { width: 100%; height: 60px; background: #0a0a12; border: 1px solid #1e1e2e; border-radius: 3px; }
        .rate-badge { display: inline-block; padding: 2px 8px; border-radius: 3px; font-size: 10px; font-weight: bold; margin-left: 8px; }
        .rate-600 { background: #4a9eff; color: #000; }
        .rate-1200 { background: #00d4ff; color: #000; }
        .rate-2400 { background: #00ff88; color: #000; }
        .record-section { margin-top: 16px; padding-top: 16px; border-top: 1px solid #1e1e2e; }
        .btn-record { background: #ff3a50; }
        .btn-record.recording { animation: pulse 1s infinite; }
        .record-timer { font-family: monospace; font-size: 14px; color: #ff3a50; margin-left: 10px; }
        .btn-play-input, .btn-play-output { background: #00d4ff; color: #000; }
        .btn-run-vocoder { background: #00ff88; color: #000; }
)CSS";

// ============================================================
// MELPE TAB HTML
// ============================================================
const char* HTML_TAB_MELPE = R"HTML(
        <!-- ============ MELPE VOCODER TAB ============ -->
        <div id="tab-melpe" class="tab-content">
            <div class="melpe-header">
                <h2>MELPe Vocoder Test</h2>
                <p>NATO STANAG 4591 - Mixed-Excitation Linear Prediction Enhanced</p>
            </div>
            
            <div class="melpe-container">
                <!-- Controls Panel -->
                <div class="melpe-controls">
                    <h3>Test Configuration</h3>
                    
                    <div class="form-group" style="margin-bottom: 12px;">
                        <label>Audio File</label>
                        <select id="melpe-file" onchange="onFileSelectionChange()">
                            <option value="OSR_us_000_0010_8k.raw">OSR Sample 0010 (Harvard Sentences)</option>
                            <option value="OSR_us_000_0011_8k.raw">OSR Sample 0011 (Harvard Sentences)</option>
                            <option value="OSR_us_000_0030_8k.raw">OSR Sample 0030 (Harvard Sentences)</option>
                            <option value="OSR_us_000_0031_8k.raw">OSR Sample 0031 (Harvard Sentences)</option>
                        </select>
                    </div>
                    
                    <div class="form-group" style="margin-bottom: 12px;">
                        <label>Bit Rate</label>
                        <select id="melpe-rate" onchange="updateRateBadge()">
                            <option value="2400" selected>2400 bps (7 bytes / 22.5ms frame)</option>
                            <option value="1200">1200 bps (11 bytes / 67.5ms frame)</option>
                            <option value="600">600 bps (7 bytes / 90ms frame)</option>
                        </select>
                    </div>
                    
                    <button class="btn btn-run-vocoder" onclick="runMelpeVocoder()" style="width: 100%;">
                        ▶ Run Loopback Test
                    </button>
                    
                    <div class="melpe-status" id="melpe-status">
                        Ready. Select a file and bit rate, then click Run.
                    </div>
                    
                    <!-- Recording Section -->
                    <div class="record-section">
                        <h3>🎤 Record Custom Audio</h3>
                        <div class="form-row" style="margin-bottom: 10px;">
                            <button class="btn btn-record" id="btn-record" onclick="toggleRecording()">
                                ⏺ Start Recording
                            </button>
                            <span class="record-timer" id="record-timer">0:00</span>
                        </div>
                        <div class="form-row">
                            <div class="form-group">
                                <label>Recording Name</label>
                                <input type="text" id="recording-name" placeholder="my_recording">
                            </div>
                            <button class="btn btn-secondary" onclick="saveRecording()" style="align-self: flex-end;">
                                💾 Save
                            </button>
                        </div>
                    </div>
                    
                    <!-- Info Section -->
                    <div style="margin-top: 16px; padding: 12px; background: #0a0a12; border-radius: 3px; font-size: 11px; color: #6a7080;">
                        <strong style="color: #00d4ff;">MELPe Specifications:</strong><br>
                        • NATO STANAG 4591 compliant<br>
                        • Rates: 2400, 1200, 600 bps<br>
                        • Audio: 8000 Hz, 16-bit signed PCM<br>
                        • Frame sizes vary by rate
                    </div>
                </div>
                
                <!-- Audio Panels -->
                <div>
                    <div class="audio-panel">
                        <div class="audio-card">
                            <h3>Input Audio <span class="rate-badge rate-2400" id="input-rate-badge">Original</span></h3>
                            <canvas class="audio-viz" id="input-waveform"></canvas>
                            <div class="audio-player">
                                <button class="btn btn-play-input" onclick="playInputAudio()">▶ Play</button>
                                <button class="btn btn-secondary" onclick="stopInputAudio()">■ Stop</button>
                                <span id="input-duration" style="margin-left: auto; font-size: 11px; color: #6a7080;">--:--</span>
                            </div>
                        </div>
                    </div>
                    
                    <div class="audio-panel">
                        <div class="audio-card">
                            <h3>Output Audio <span class="rate-badge" id="output-rate-badge">Processed</span></h3>
                            <canvas class="audio-viz" id="output-waveform"></canvas>
                            <div class="audio-player">
                                <button class="btn btn-play-output" onclick="playOutputAudio()">▶ Play</button>
                                <button class="btn btn-secondary" onclick="stopOutputAudio()">■ Stop</button>
                                <span id="output-duration" style="margin-left: auto; font-size: 11px; color: #6a7080;">--:--</span>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
)HTML";

// ============================================================
// MELPE TAB JAVASCRIPT
// ============================================================
const char* HTML_JS_MELPE = R"JS(
        // MELPe audio variables
        let audioContextInput = null;
        let audioContextOutput = null;
        let inputAudioBuffer = null;
        let outputAudioBuffer = null;
        let inputSourceNode = null;
        let outputSourceNode = null;
        let isRecording = false;
        let mediaRecorder = null;
        let recordedChunks = [];
        let recordingStartTime = null;
        let recordingTimerInterval = null;
        
        function updateRateBadge() {
            const rate = document.getElementById('melpe-rate').value;
            const badge = document.getElementById('output-rate-badge');
            badge.className = 'rate-badge rate-' + rate;
            badge.textContent = rate + ' bps';
        }
        
        async function loadMelpeFiles() {
            try {
                const response = await fetch('/melpe-list-recordings');
                const data = await response.json();
                if (data.recordings && data.recordings.length > 0) {
                    const select = document.getElementById('melpe-file');
                    let hasCustomSection = false;
                    data.recordings.forEach(rec => {
                        if (!hasCustomSection) {
                            const opt = document.createElement('option');
                            opt.disabled = true;
                            opt.textContent = '── Custom Recordings ──';
                            select.appendChild(opt);
                            hasCustomSection = true;
                        }
                        const opt = document.createElement('option');
                        opt.value = rec.filename;
                        opt.textContent = rec.name + ' (' + rec.duration.toFixed(1) + 's)';
                        select.appendChild(opt);
                    });
                }
            } catch (e) {
                console.log('Could not load custom recordings:', e);
            }
            onFileSelectionChange();
        }
        
        async function onFileSelectionChange() {
            const filename = document.getElementById('melpe-file').value;
            const status = document.getElementById('melpe-status');
            status.className = 'melpe-status';
            status.textContent = 'Loading ' + filename + '...';
            
            try {
                const response = await fetch('/melpe-audio?file=' + encodeURIComponent(filename));
                if (!response.ok) throw new Error('File not found');
                
                const arrayBuffer = await response.arrayBuffer();
                const pcmData = new Int16Array(arrayBuffer);
                
                if (!audioContextInput) {
                    audioContextInput = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 8000 });
                }
                
                const floatData = new Float32Array(pcmData.length);
                for (let i = 0; i < pcmData.length; i++) {
                    floatData[i] = pcmData[i] / 32768.0;
                }
                
                inputAudioBuffer = audioContextInput.createBuffer(1, floatData.length, 8000);
                inputAudioBuffer.getChannelData(0).set(floatData);
                
                const duration = floatData.length / 8000;
                document.getElementById('input-duration').textContent = 
                    Math.floor(duration / 60) + ':' + Math.floor(duration % 60).toString().padStart(2, '0');
                
                drawWaveform('input-waveform', floatData);
                
                status.className = 'melpe-status success';
                status.textContent = 'Loaded ' + filename + ' (' + duration.toFixed(1) + 's)';
            } catch (e) {
                status.className = 'melpe-status error';
                status.textContent = 'Error loading file: ' + e.message;
            }
        }
        
        function drawWaveform(canvasId, data) {
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
        
        async function runMelpeVocoder() {
            const filename = document.getElementById('melpe-file').value;
            const rate = document.getElementById('melpe-rate').value;
            const status = document.getElementById('melpe-status');
            
            status.className = 'melpe-status';
            status.textContent = 'Running vocoder at ' + rate + ' bps...';
            
            try {
                const response = await fetch('/melpe-run?input=' + encodeURIComponent(filename) + '&rate=' + rate);
                const data = await response.json();
                
                if (data.success) {
                    status.className = 'melpe-status success';
                    status.textContent = data.message;
                    await loadOutputAudio(data.output_file, rate);
                } else {
                    status.className = 'melpe-status error';
                    status.textContent = 'Error: ' + data.message;
                }
            } catch (e) {
                status.className = 'melpe-status error';
                status.textContent = 'Error: ' + e.message;
            }
        }
        
        async function loadOutputAudio(filename, rate) {
            try {
                const response = await fetch('/melpe-output?file=' + encodeURIComponent(filename));
                if (!response.ok) throw new Error('Output file not found');
                
                const arrayBuffer = await response.arrayBuffer();
                const pcmData = new Int16Array(arrayBuffer);
                
                if (!audioContextOutput) {
                    audioContextOutput = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 8000 });
                }
                
                const floatData = new Float32Array(pcmData.length);
                for (let i = 0; i < pcmData.length; i++) {
                    floatData[i] = pcmData[i] / 32768.0;
                }
                
                outputAudioBuffer = audioContextOutput.createBuffer(1, floatData.length, 8000);
                outputAudioBuffer.getChannelData(0).set(floatData);
                
                const duration = floatData.length / 8000;
                document.getElementById('output-duration').textContent = 
                    Math.floor(duration / 60) + ':' + Math.floor(duration % 60).toString().padStart(2, '0');
                
                drawWaveform('output-waveform', floatData);
                
                const badge = document.getElementById('output-rate-badge');
                badge.className = 'rate-badge rate-' + rate;
                badge.textContent = rate + ' bps';
            } catch (e) {
                console.error('Error loading output:', e);
            }
        }
        
        function playInputAudio() {
            if (!inputAudioBuffer || !audioContextInput) return;
            stopInputAudio();
            inputSourceNode = audioContextInput.createBufferSource();
            inputSourceNode.buffer = inputAudioBuffer;
            inputSourceNode.connect(audioContextInput.destination);
            inputSourceNode.start();
        }
        
        function stopInputAudio() {
            if (inputSourceNode) {
                try { inputSourceNode.stop(); } catch (e) {}
                inputSourceNode = null;
            }
        }
        
        function playOutputAudio() {
            if (!outputAudioBuffer || !audioContextOutput) return;
            stopOutputAudio();
            outputSourceNode = audioContextOutput.createBufferSource();
            outputSourceNode.buffer = outputAudioBuffer;
            outputSourceNode.connect(audioContextOutput.destination);
            outputSourceNode.start();
        }
        
        function stopOutputAudio() {
            if (outputSourceNode) {
                try { outputSourceNode.stop(); } catch (e) {}
                outputSourceNode = null;
            }
        }
        
        async function toggleRecording() {
            const btn = document.getElementById('btn-record');
            
            if (!isRecording) {
                try {
                    const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
                    mediaRecorder = new MediaRecorder(stream);
                    recordedChunks = [];
                    
                    mediaRecorder.ondataavailable = e => {
                        if (e.data.size > 0) recordedChunks.push(e.data);
                    };
                    
                    mediaRecorder.start();
                    isRecording = true;
                    recordingStartTime = Date.now();
                    
                    btn.textContent = '⏹ Stop Recording';
                    btn.classList.add('recording');
                    
                    recordingTimerInterval = setInterval(() => {
                        const elapsed = Math.floor((Date.now() - recordingStartTime) / 1000);
                        document.getElementById('record-timer').textContent = 
                            Math.floor(elapsed / 60) + ':' + (elapsed % 60).toString().padStart(2, '0');
                    }, 100);
                } catch (e) {
                    alert('Could not access microphone: ' + e.message);
                }
            } else {
                mediaRecorder.stop();
                mediaRecorder.stream.getTracks().forEach(t => t.stop());
                isRecording = false;
                
                btn.textContent = '⏺ Start Recording';
                btn.classList.remove('recording');
                clearInterval(recordingTimerInterval);
                
                setTimeout(convertRecordingToPcm, 100);
            }
        }
        
        async function convertRecordingToPcm() {
            const blob = new Blob(recordedChunks, { type: 'audio/webm' });
            const arrayBuffer = await blob.arrayBuffer();
            
            const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
            const audioBuffer = await audioCtx.decodeAudioData(arrayBuffer);
            
            const offlineCtx = new OfflineAudioContext(1, audioBuffer.duration * 8000, 8000);
            const source = offlineCtx.createBufferSource();
            source.buffer = audioBuffer;
            source.connect(offlineCtx.destination);
            source.start();
            
            const resampled = await offlineCtx.startRendering();
            const pcmFloat = resampled.getChannelData(0);
            
            const pcm16 = new Int16Array(pcmFloat.length);
            for (let i = 0; i < pcmFloat.length; i++) {
                pcm16[i] = Math.max(-32768, Math.min(32767, Math.floor(pcmFloat[i] * 32767)));
            }
            
            window.lastRecordedPcm = pcm16;
            
            if (!audioContextInput) {
                audioContextInput = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 8000 });
            }
            inputAudioBuffer = audioContextInput.createBuffer(1, pcmFloat.length, 8000);
            inputAudioBuffer.getChannelData(0).set(pcmFloat);
            
            const duration = pcmFloat.length / 8000;
            document.getElementById('input-duration').textContent = 
                Math.floor(duration / 60) + ':' + Math.floor(duration % 60).toString().padStart(2, '0');
            drawWaveform('input-waveform', pcmFloat);
            
            document.getElementById('melpe-status').className = 'melpe-status success';
            document.getElementById('melpe-status').textContent = 'Recording captured (' + duration.toFixed(1) + 's). Click Save to store.';
        }
        
        async function saveRecording() {
            if (!window.lastRecordedPcm) {
                alert('No recording to save. Record something first.');
                return;
            }
            
            let name = document.getElementById('recording-name').value.trim();
            if (!name) name = 'recording_' + Date.now();
            
            const bytes = new Uint8Array(window.lastRecordedPcm.buffer);
            let binary = '';
            for (let i = 0; i < bytes.length; i++) {
                binary += String.fromCharCode(bytes[i]);
            }
            const base64 = btoa(binary);
            
            try {
                const response = await fetch('/melpe-save-recording', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ filename: name + '.pcm', pcm_data: base64 })
                });
                const data = await response.json();
                
                if (data.success) {
                    document.getElementById('melpe-status').className = 'melpe-status success';
                    document.getElementById('melpe-status').textContent = 'Saved as ' + data.filename;
                    
                    const select = document.getElementById('melpe-file');
                    const opt = document.createElement('option');
                    opt.value = data.filename;
                    opt.textContent = name + ' (' + data.duration.toFixed(1) + 's)';
                    opt.selected = true;
                    select.appendChild(opt);
                } else {
                    alert('Save failed: ' + data.message);
                }
            } catch (e) {
                alert('Save error: ' + e.message);
            }
        }
)JS";

} // namespace test_gui
