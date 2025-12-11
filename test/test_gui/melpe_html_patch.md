# MELPe HTML Content Patch

## Instructions
Apply these sections to `html_content.h` in order:
1. Add CSS (inside `<style>` tag)
2. Add Tab Button (in `.tabs` div)
3. Add Tab Content (after other tab-content divs)
4. Add JavaScript (inside `<script>` tag)

---

## 1. CSS - Add before closing `</style>` tag

```css
        /* MELPe Vocoder tab styles */
        .melpe-container { background: #16213e; padding: 20px; border-radius: 0 8px 8px 8px; }
        .melpe-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
        .melpe-header h2 { margin: 0; color: #00d4ff; }
        .melpe-controls { display: flex; gap: 20px; margin-bottom: 20px; flex-wrap: wrap; align-items: flex-end; }
        .melpe-controls .field { display: flex; flex-direction: column; }
        .melpe-controls label { margin-bottom: 5px; color: #aaa; font-size: 12px; }
        .melpe-controls select { padding: 10px 15px; border: 1px solid #333; border-radius: 4px; 
                                 background: #0f0f23; color: #fff; min-width: 180px; }
        .melpe-status { padding: 10px 15px; border-radius: 4px; margin-bottom: 15px; background: #0f3460; color: #aaa; }
        .melpe-status.running { background: #1e3a5f; color: #fff; }
        .melpe-status.success { background: #1e5f3a; color: #fff; }
        .melpe-status.error { background: #5f1e1e; color: #fff; }
        .audio-panel { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-top: 20px; }
        .audio-card { background: #0f0f23; border: 1px solid #333; border-radius: 8px; padding: 20px; }
        .audio-card h3 { color: #00d4ff; margin: 0 0 15px 0; font-size: 14px; }
        .audio-card .file-info { font-family: 'Consolas', monospace; font-size: 11px; color: #888; 
                                 margin-bottom: 15px; word-break: break-all; }
        .audio-player { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
        .btn-play { padding: 12px 20px; border: none; border-radius: 4px; cursor: pointer; 
                    font-weight: bold; display: flex; align-items: center; gap: 8px; font-family: inherit; }
        .btn-play-input { background: #5f5fff; color: #fff; }
        .btn-play-input:hover { background: #4a4acc; }
        .btn-play-output { background: #5fff5f; color: #000; }
        .btn-play-output:hover { background: #4acc4a; }
        .btn-play:disabled { background: #444; color: #888; cursor: not-allowed; }
        .btn-stop-audio { background: #ff4757; color: #fff; padding: 12px 15px; }
        .btn-stop-audio:hover { background: #cc3a47; }
        .btn-run-vocoder { background: #00d4ff; color: #000; padding: 12px 25px; }
        .btn-run-vocoder:hover { background: #00a8cc; }
        .btn-run-vocoder:disabled { background: #444; color: #888; cursor: not-allowed; }
        .audio-viz { height: 60px; background: #0a0a1a; border-radius: 4px; margin-top: 10px; 
                     display: flex; align-items: center; justify-content: center; color: #444; }
        .audio-viz canvas { width: 100%; height: 100%; }
        .melpe-info { background: #0f3460; padding: 15px; border-radius: 8px; margin-top: 20px; }
        .melpe-info h4 { color: #00d4ff; margin: 0 0 10px 0; }
        .melpe-info p { color: #aaa; margin: 5px 0; font-size: 13px; }
        .melpe-info code { background: #0f0f23; padding: 2px 6px; border-radius: 3px; color: #5fff5f; }
        .rate-badge { display: inline-block; padding: 3px 8px; border-radius: 3px; font-size: 10px; 
                      font-weight: bold; margin-left: 10px; }
        .rate-600 { background: #ff9f43; color: #000; }
        .rate-1200 { background: #5f5fff; color: #fff; }
        .rate-2400 { background: #5fff5f; color: #000; }
        
        /* Recording styles */
        .record-section { background: #0f3460; padding: 15px; border-radius: 8px; margin-bottom: 20px; }
        .record-section h4 { color: #ff4757; margin: 0 0 10px 0; }
        .record-controls { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
        .btn-record { padding: 12px 20px; border: none; border-radius: 4px; cursor: pointer; 
                      font-weight: bold; display: flex; align-items: center; gap: 8px; font-family: inherit; }
        .btn-record-start { background: #ff4757; color: #fff; }
        .btn-record-start:hover { background: #cc3a47; }
        .btn-record-start.recording { background: #ff0000; animation: pulse 1s infinite; }
        .btn-record-stop { background: #444; color: #fff; }
        .btn-record-stop:hover { background: #555; }
        .btn-record-save { background: #5fff5f; color: #000; }
        .btn-record-save:hover { background: #4acc4a; }
        .btn-record-save:disabled { background: #444; color: #888; cursor: not-allowed; }
        .record-status { margin-left: 15px; color: #aaa; font-size: 13px; }
        .record-status.recording { color: #ff4757; font-weight: bold; }
        .record-timer { font-family: 'Consolas', monospace; font-size: 16px; color: #ff4757; margin-left: 10px; }
        .record-name-input { padding: 8px 12px; border: 1px solid #333; border-radius: 4px; 
                             background: #0f0f23; color: #fff; width: 200px; }
```

---

## 2. Tab Button - Add after the Reports tab button in `.tabs` div

Find:
```html
            <button class="tab" onclick="showTab('reports')">Reports</button>
```

Add after it:
```html
            <button class="tab" onclick="showTab('melpe')">MELPe Vocoder</button>
```

---

## 3. Tab Content - Add after `<!-- ============ REPORTS TAB ============ -->` section, before closing `</div>` of container

```html
        <!-- ============ MELPE VOCODER TAB ============ -->
        <div id="tab-melpe" class="tab-content">
            <div class="melpe-container">
                <div class="melpe-header">
                    <h2>🎵 MELPe Vocoder Test</h2>
                    <span class="rate-badge rate-2400" id="rate-badge">2400 bps</span>
                </div>
                
                <div class="melpe-controls">
                    <div class="field">
                        <label>Test Audio File</label>
                        <select id="melpe-input" onchange="onFileSelectionChange()">
                            <option value="OSR_us_000_0010_8k.raw">Female Speaker - Set 1 (~34s)</option>
                            <option value="OSR_us_000_0011_8k.raw">Female Speaker - Set 2 (~33s)</option>
                            <option value="OSR_us_000_0030_8k.raw">Male Speaker - Set 1 (~47s)</option>
                            <option value="OSR_us_000_0031_8k.raw">Male Speaker - Set 2 (~42s)</option>
                        </select>
                    </div>
                    <div class="field">
                        <label>Bit Rate</label>
                        <select id="melpe-rate" onchange="updateRateBadge()">
                            <option value="2400">2400 bps (High Quality)</option>
                            <option value="1200">1200 bps (Medium)</option>
                            <option value="600">600 bps (Low Bandwidth)</option>
                        </select>
                    </div>
                    <div class="field">
                        <label>&nbsp;</label>
                        <button class="btn-run-vocoder" id="btn-run-melpe" onclick="runMelpeVocoder()">
                            🔊 Run Loopback Test
                        </button>
                    </div>
                </div>
                
                <div class="record-section">
                    <h4>🎙️ Record Your Own Audio</h4>
                    <div class="record-controls">
                        <button class="btn-record btn-record-start" id="btn-record" onclick="toggleRecording()">
                            🎵 Start Recording
                        </button>
                        <span class="record-timer" id="record-timer" style="display:none;">00:00</span>
                        <input type="text" class="record-name-input" id="record-name" placeholder="my_recording" maxlength="30">
                        <button class="btn-record btn-record-save" id="btn-save-recording" onclick="saveRecording()" disabled>
                            💾 Save Recording
                        </button>
                        <span class="record-status" id="record-status">Click to start recording (8kHz mono)</span>
                    </div>
                </div>
                
                <div class="melpe-status" id="melpe-status">
                    Ready - Select a test file and bit rate, then click "Run Loopback Test"
                </div>
                
                <div class="audio-panel">
                    <div class="audio-card">
                        <h3>📥 Input Audio (Original)</h3>
                        <div class="file-info" id="input-file-info">No file loaded</div>
                        <div class="audio-player">
                            <button class="btn-play btn-play-input" id="btn-play-input" onclick="playInputAudio()" disabled>
                                ▶ Play Input
                            </button>
                            <button class="btn-play btn-stop-audio" id="btn-stop-input" onclick="stopInputAudio()" style="display:none;">
                                ⏹ Stop
                            </button>
                        </div>
                        <div class="audio-viz" id="input-viz">
                            <span>Load audio to visualize</span>
                        </div>
                    </div>
                    <div class="audio-card">
                        <h3>📤 Output Audio (Processed)</h3>
                        <div class="file-info" id="output-file-info">Run vocoder to generate output</div>
                        <div class="audio-player">
                            <button class="btn-play btn-play-output" id="btn-play-output" onclick="playOutputAudio()" disabled>
                                ▶ Play Output
                            </button>
                            <button class="btn-play btn-stop-audio" id="btn-stop-output" onclick="stopOutputAudio()" style="display:none;">
                                ⏹ Stop
                            </button>
                        </div>
                        <div class="audio-viz" id="output-viz">
                            <span>Output will appear here</span>
                        </div>
                    </div>
                </div>
                
                <div class="melpe-info">
                    <h4>ℹ️ About MELPe Vocoder</h4>
                    <p><strong>NATO STANAG 4591</strong> - Mixed-Excitation Linear Prediction Enhanced</p>
                    <p>Supported rates: <code>2400 bps</code> (7 bytes/22.5ms), <code>1200 bps</code> (11 bytes/67.5ms), <code>600 bps</code> (7 bytes/90ms)</p>
                    <p>Audio format: <code>8000 Hz, 16-bit signed PCM, mono</code></p>
                    <p>Test files from Open Speech Repository (Harvard Sentences)</p>
                </div>
            </div>
        </div><!-- end tab-melpe -->
```

---

## 4. JavaScript - Add inside `<script>` tag

### 4a. Add these variables near the top of the script (after other variable declarations):

```javascript
        // ============ MELPE VOCODER ============
        let audioContextInput = null;
        let audioContextOutput = null;
        let inputAudioBuffer = null;
        let outputAudioBuffer = null;
        let inputSourceNode = null;
        let outputSourceNode = null;
        let melpeOutputFile = '';
        
        // Recording state
        let mediaRecorder = null;
        let recordedChunks = [];
        let recordingStream = null;
        let recordedPcmData = null;
        let recordingStartTime = null;
        let recordingTimer = null;
```

### 4b. Modify showTab function to load MELPe files:

Find the showTab function and add this case:
```javascript
            if (name === 'melpe') {
                loadMelpeFiles();
                loadCustomRecordings();
            }
```

### 4c. Add these functions (add before the closing `</script>` tag):

```javascript
        // ============ MELPE VOCODER FUNCTIONS ============
        function updateRateBadge() {
            const rate = document.getElementById('melpe-rate').value;
            const badge = document.getElementById('rate-badge');
            badge.textContent = rate + ' bps';
            badge.className = 'rate-badge rate-' + rate;
        }
        
        async function loadMelpeFiles() {
            const inputSelect = document.getElementById('melpe-input');
            const selectedFile = inputSelect.value;
            
            document.getElementById('input-file-info').textContent = 'examples/melpe_test_audio/' + selectedFile;
            
            try {
                const response = await fetch('/melpe-audio?file=' + encodeURIComponent(selectedFile));
                if (response.ok) {
                    const arrayBuffer = await response.arrayBuffer();
                    
                    if (!audioContextInput) {
                        audioContextInput = new (window.AudioContext || window.webkitAudioContext)();
                    }
                    
                    const dataView = new DataView(arrayBuffer);
                    const numSamples = arrayBuffer.byteLength / 2;
                    inputAudioBuffer = audioContextInput.createBuffer(1, numSamples, 8000);
                    const channelData = inputAudioBuffer.getChannelData(0);
                    
                    for (let i = 0; i < numSamples; i++) {
                        const int16 = dataView.getInt16(i * 2, true);
                        channelData[i] = int16 / 32768.0;
                    }
                    
                    document.getElementById('btn-play-input').disabled = false;
                    const duration = (numSamples / 8000).toFixed(1);
                    document.getElementById('input-file-info').textContent = 
                        'examples/melpe_test_audio/' + selectedFile + ' (' + duration + 's)';
                    
                    drawWaveform('input-viz', channelData);
                }
            } catch (err) {
                console.error('Failed to load audio:', err);
                document.getElementById('input-file-info').textContent = 'Error loading: ' + selectedFile;
            }
        }
        
        function drawWaveform(containerId, data) {
            const container = document.getElementById(containerId);
            container.innerHTML = '<canvas></canvas>';
            const canvas = container.querySelector('canvas');
            const ctx = canvas.getContext('2d');
            
            canvas.width = container.clientWidth || 300;
            canvas.height = container.clientHeight || 60;
            
            ctx.fillStyle = '#0a0a1a';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            
            ctx.strokeStyle = '#00d4ff';
            ctx.lineWidth = 1;
            ctx.beginPath();
            
            const step = Math.floor(data.length / canvas.width);
            const mid = canvas.height / 2;
            
            for (let i = 0; i < canvas.width; i++) {
                const idx = i * step;
                let min = 1, max = -1;
                for (let j = 0; j < step && idx + j < data.length; j++) {
                    const v = data[idx + j];
                    if (v < min) min = v;
                    if (v > max) max = v;
                }
                const y1 = mid + min * mid * 0.9;
                const y2 = mid + max * mid * 0.9;
                ctx.moveTo(i, y1);
                ctx.lineTo(i, y2);
            }
            ctx.stroke();
        }
        
        async function runMelpeVocoder() {
            const inputFile = document.getElementById('melpe-input').value;
            const rate = document.getElementById('melpe-rate').value;
            const statusDiv = document.getElementById('melpe-status');
            const btn = document.getElementById('btn-run-melpe');
            
            btn.disabled = true;
            statusDiv.className = 'melpe-status running';
            statusDiv.textContent = 'Running MELPe vocoder at ' + rate + ' bps...';
            
            try {
                const response = await fetch('/melpe-run?input=' + encodeURIComponent(inputFile) + 
                                            '&rate=' + rate);
                const result = await response.json();
                
                if (result.success) {
                    statusDiv.className = 'melpe-status success';
                    statusDiv.textContent = '✓ Vocoder complete! ' + result.message;
                    melpeOutputFile = result.output_file;
                    document.getElementById('output-file-info').textContent = result.output_file;
                    
                    await loadOutputAudio(result.output_file);
                } else {
                    statusDiv.className = 'melpe-status error';
                    statusDiv.textContent = '✗ Error: ' + result.message;
                }
            } catch (err) {
                statusDiv.className = 'melpe-status error';
                statusDiv.textContent = '✗ Error: ' + err.message;
            } finally {
                btn.disabled = false;
            }
        }
        
        async function loadOutputAudio(filename) {
            try {
                const response = await fetch('/melpe-output?file=' + encodeURIComponent(filename));
                if (response.ok) {
                    const arrayBuffer = await response.arrayBuffer();
                    
                    if (!audioContextOutput) {
                        audioContextOutput = new (window.AudioContext || window.webkitAudioContext)();
                    }
                    
                    const dataView = new DataView(arrayBuffer);
                    const numSamples = arrayBuffer.byteLength / 2;
                    outputAudioBuffer = audioContextOutput.createBuffer(1, numSamples, 8000);
                    const channelData = outputAudioBuffer.getChannelData(0);
                    
                    for (let i = 0; i < numSamples; i++) {
                        const int16 = dataView.getInt16(i * 2, true);
                        channelData[i] = int16 / 32768.0;
                    }
                    
                    document.getElementById('btn-play-output').disabled = false;
                    const duration = (numSamples / 8000).toFixed(1);
                    document.getElementById('output-file-info').textContent = filename + ' (' + duration + 's)';
                    
                    drawWaveform('output-viz', channelData);
                }
            } catch (err) {
                console.error('Failed to load output audio:', err);
            }
        }
        
        function playInputAudio() {
            if (!inputAudioBuffer) return;
            if (audioContextInput.state === 'suspended') audioContextInput.resume();
            if (inputSourceNode) inputSourceNode.stop();
            
            inputSourceNode = audioContextInput.createBufferSource();
            inputSourceNode.buffer = inputAudioBuffer;
            inputSourceNode.connect(audioContextInput.destination);
            inputSourceNode.onended = () => {
                document.getElementById('btn-play-input').style.display = '';
                document.getElementById('btn-stop-input').style.display = 'none';
            };
            inputSourceNode.start();
            
            document.getElementById('btn-play-input').style.display = 'none';
            document.getElementById('btn-stop-input').style.display = '';
        }
        
        function stopInputAudio() {
            if (inputSourceNode) { inputSourceNode.stop(); inputSourceNode = null; }
            document.getElementById('btn-play-input').style.display = '';
            document.getElementById('btn-stop-input').style.display = 'none';
        }
        
        function playOutputAudio() {
            if (!outputAudioBuffer) return;
            if (audioContextOutput.state === 'suspended') audioContextOutput.resume();
            if (outputSourceNode) outputSourceNode.stop();
            
            outputSourceNode = audioContextOutput.createBufferSource();
            outputSourceNode.buffer = outputAudioBuffer;
            outputSourceNode.connect(audioContextOutput.destination);
            outputSourceNode.onended = () => {
                document.getElementById('btn-play-output').style.display = '';
                document.getElementById('btn-stop-output').style.display = 'none';
            };
            outputSourceNode.start();
            
            document.getElementById('btn-play-output').style.display = 'none';
            document.getElementById('btn-stop-output').style.display = '';
        }
        
        function stopOutputAudio() {
            if (outputSourceNode) { outputSourceNode.stop(); outputSourceNode = null; }
            document.getElementById('btn-play-output').style.display = '';
            document.getElementById('btn-stop-output').style.display = 'none';
        }
        
        function onFileSelectionChange() { loadMelpeFiles(); }
        
        // ============ AUDIO RECORDING ============
        async function toggleRecording() {
            const btn = document.getElementById('btn-record');
            const status = document.getElementById('record-status');
            const timer = document.getElementById('record-timer');
            
            if (mediaRecorder && mediaRecorder.state === 'recording') {
                mediaRecorder.stop();
                btn.innerHTML = '🎵 Start Recording';
                btn.classList.remove('recording');
                status.textContent = 'Processing...';
                status.classList.remove('recording');
                timer.style.display = 'none';
                clearInterval(recordingTimer);
            } else {
                try {
                    recordedChunks = [];
                    recordedPcmData = null;
                    
                    recordingStream = await navigator.mediaDevices.getUserMedia({ 
                        audio: { sampleRate: 48000, channelCount: 1, echoCancellation: true, noiseSuppression: true } 
                    });
                    
                    mediaRecorder = new MediaRecorder(recordingStream, { mimeType: 'audio/webm' });
                    
                    mediaRecorder.ondataavailable = (e) => {
                        if (e.data.size > 0) recordedChunks.push(e.data);
                    };
                    
                    mediaRecorder.onstop = async () => {
                        recordingStream.getTracks().forEach(track => track.stop());
                        status.textContent = 'Converting to 8kHz PCM...';
                        await convertRecordingToPcm();
                    };
                    
                    mediaRecorder.start(100);
                    recordingStartTime = Date.now();
                    
                    btn.innerHTML = '⏹ Stop Recording';
                    btn.classList.add('recording');
                    status.textContent = 'Recording...';
                    status.classList.add('recording');
                    timer.style.display = 'inline';
                    timer.textContent = '00:00';
                    
                    recordingTimer = setInterval(() => {
                        const elapsed = Math.floor((Date.now() - recordingStartTime) / 1000);
                        const mins = Math.floor(elapsed / 60).toString().padStart(2, '0');
                        const secs = (elapsed % 60).toString().padStart(2, '0');
                        timer.textContent = mins + ':' + secs;
                    }, 1000);
                    
                    document.getElementById('btn-save-recording').disabled = true;
                } catch (err) {
                    status.textContent = 'Error: ' + err.message;
                }
            }
        }
        
        async function convertRecordingToPcm() {
            const status = document.getElementById('record-status');
            const saveBtn = document.getElementById('btn-save-recording');
            
            try {
                const blob = new Blob(recordedChunks, { type: 'audio/webm' });
                const arrayBuffer = await blob.arrayBuffer();
                
                const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
                const audioBuffer = await audioCtx.decodeAudioData(arrayBuffer);
                
                const offlineCtx = new OfflineAudioContext(1, Math.ceil(audioBuffer.duration * 8000), 8000);
                const source = offlineCtx.createBufferSource();
                source.buffer = audioBuffer;
                source.connect(offlineCtx.destination);
                source.start();
                
                const resampledBuffer = await offlineCtx.startRendering();
                const floatData = resampledBuffer.getChannelData(0);
                
                recordedPcmData = new Int16Array(floatData.length);
                for (let i = 0; i < floatData.length; i++) {
                    const s = Math.max(-1, Math.min(1, floatData[i]));
                    recordedPcmData[i] = s < 0 ? s * 32768 : s * 32767;
                }
                
                const duration = (recordedPcmData.length / 8000).toFixed(1);
                status.textContent = 'Ready to save (' + duration + 's at 8kHz)';
                saveBtn.disabled = false;
                
                drawWaveform('input-viz', floatData);
            } catch (err) {
                status.textContent = 'Conversion error: ' + err.message;
            }
        }
        
        async function saveRecording() {
            if (!recordedPcmData) return;
            
            const nameInput = document.getElementById('record-name');
            const status = document.getElementById('record-status');
            const saveBtn = document.getElementById('btn-save-recording');
            
            let baseName = nameInput.value.trim() || 'recording';
            baseName = baseName.replace(/[^a-zA-Z0-9_-]/g, '_');
            const timestamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
            const filename = baseName + '_' + timestamp + '_8k.pcm';
            
            status.textContent = 'Saving...';
            saveBtn.disabled = true;
            
            try {
                const uint8 = new Uint8Array(recordedPcmData.buffer);
                let binary = '';
                for (let i = 0; i < uint8.length; i++) binary += String.fromCharCode(uint8[i]);
                const base64Data = btoa(binary);
                
                const response = await fetch('/melpe-save-recording', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ filename: filename, pcm_data: base64Data })
                });
                
                const result = await response.json();
                
                if (result.success) {
                    status.textContent = 'Saved: ' + filename;
                    
                    const select = document.getElementById('melpe-input');
                    const option = document.createElement('option');
                    option.value = filename;
                    option.textContent = '🎵 ' + baseName + ' (' + (recordedPcmData.length / 8000).toFixed(1) + 's)';
                    select.appendChild(option);
                    select.value = filename;
                    
                    loadMelpeFiles();
                    recordedPcmData = null;
                    nameInput.value = '';
                } else {
                    status.textContent = 'Save failed: ' + result.message;
                    saveBtn.disabled = false;
                }
            } catch (err) {
                status.textContent = 'Save error: ' + err.message;
                saveBtn.disabled = false;
            }
        }
        
        async function loadCustomRecordings() {
            try {
                const response = await fetch('/melpe-list-recordings');
                const result = await response.json();
                
                if (result.recordings && result.recordings.length > 0) {
                    const select = document.getElementById('melpe-input');
                    
                    // Check if separator already exists
                    const existingSep = Array.from(select.options).find(o => o.disabled && o.textContent.includes('Your Recordings'));
                    if (!existingSep) {
                        const separator = document.createElement('option');
                        separator.disabled = true;
                        separator.textContent = '── Your Recordings ──';
                        select.appendChild(separator);
                        
                        result.recordings.forEach(rec => {
                            const option = document.createElement('option');
                            option.value = rec.filename;
                            option.textContent = '🎵 ' + rec.name + ' (' + rec.duration + 's)';
                            select.appendChild(option);
                        });
                    }
                }
            } catch (err) {
                console.error('Failed to load recordings:', err);
            }
        }
```
