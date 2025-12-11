#pragma once
/**
 * @file html_tab_tests.h
 * @brief Run Tests tab HTML/JS for M110A Test GUI
 * 
 * Supports both PhoenixNest and Brain backends
 */

namespace test_gui {

// ============================================================
// RUN TESTS TAB HTML
// ============================================================
const char* HTML_TAB_TESTS = R"HTML(
        <!-- ============ RUN TESTS TAB ============ -->
        <div id="tab-tests" class="tab-content active">
            <div class="test-layout">
                <!-- LEFT: Control Panel -->
                <div class="control-panel">
                    
                    <!-- Backend Selection -->
                    <div class="control-section">
                        <h2>Test Backend</h2>
                        <div class="radio-group">
                            <div class="radio-item selected" onclick="selectBackend('phoenixnest')">
                                <input type="radio" name="backend" id="backend-phoenixnest" checked>
                                <div class="radio-content">
                                    <label for="backend-phoenixnest">🚀 PhoenixNest</label>
                                    <span class="radio-desc">Your M110A implementation (exhaustive_test.exe)</span>
                                </div>
                            </div>
                            <div class="radio-item" onclick="selectBackend('brain')">
                                <input type="radio" name="backend" id="backend-brain">
                                <div class="radio-content">
                                    <label for="backend-brain">🧠 Brain Modem</label>
                                    <span class="radio-desc">Paul Brain's reference (brain_exhaustive_test.exe)</span>
                                </div>
                            </div>
                        </div>
                    </div>
                    
                    <!-- Test Configuration -->
                    <div class="control-section">
                        <h2>Test Configuration</h2>
                        <div class="form-row">
                            <div class="form-group">
                                <label>Preset</label>
                                <select id="test-preset" onchange="loadPreset()">
                                    <option value="quick">Quick Smoke Test (1 min)</option>
                                    <option value="standard" selected>Standard Test (3 min)</option>
                                    <option value="extended">Extended Test (10 min)</option>
                                    <option value="overnight">Overnight Soak (60 min)</option>
                                </select>
                            </div>
                        </div>
                        <div class="form-row">
                            <div class="form-group narrow"><label>Duration</label><input type="number" id="duration" value="3" min="1"></div>
                            <div class="form-group narrow">
                                <label>Unit</label>
                                <select id="duration-unit">
                                    <option value="min" selected>Minutes</option>
                                    <option value="sec">Seconds</option>
                                    <option value="hr">Hours</option>
                                </select>
                            </div>
                            <div class="form-group narrow"><label>RNG Seed</label><input type="number" id="rng-seed" value="42"></div>
                        </div>
                    </div>
                    
                    <!-- Mode Selection -->
                    <div class="control-section">
                        <h2>Modes</h2>
                        <div class="quick-select">
                            <button class="quick-btn" onclick="selectModes('all')">All</button>
                            <button class="quick-btn" onclick="selectModes('none')">None</button>
                            <button class="quick-btn" onclick="selectModes('short')">Short</button>
                            <button class="quick-btn" onclick="selectModes('long')">Long</button>
                            <button class="quick-btn" onclick="selectModes('fast')">Fast Only</button>
                        </div>
                        <div class="checkbox-grid three-col">
                            <div class="checkbox-item"><input type="checkbox" id="mode-75s" checked><label for="mode-75s">75S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mode-75l" checked><label for="mode-75l">75L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mode-150s" checked><label for="mode-150s">150S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mode-150l" checked><label for="mode-150l">150L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mode-300s" checked><label for="mode-300s">300S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mode-300l" checked><label for="mode-300l">300L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mode-600s" checked><label for="mode-600s">600S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mode-600l" checked><label for="mode-600l">600L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mode-1200s" checked><label for="mode-1200s">1200S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mode-1200l" checked><label for="mode-1200l">1200L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mode-2400s" checked><label for="mode-2400s">2400S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mode-2400l" checked><label for="mode-2400l">2400L</label></div>
                        </div>
                    </div>
                    
                    <!-- Test Categories (PhoenixNest only) -->
                    <div class="control-section" id="pn-categories">
                        <h2>Test Categories</h2>
                        <div class="checkbox-grid">
                            <div class="checkbox-item"><input type="checkbox" id="cat-clean" checked><label for="cat-clean">Clean Loopback</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-awgn" checked><label for="cat-awgn">AWGN Channel</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-multipath" checked><label for="cat-multipath">Multipath</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-freqoff" checked><label for="cat-freqoff">Freq Offset</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-sizes" checked><label for="cat-sizes">Message Sizes</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-random" checked><label for="cat-random">Random Data</label></div>
                        </div>
                    </div>
                    
                    <!-- Channel Parameters -->
                    <div class="control-section" id="channel-params">
                        <h2>Channel Parameters</h2>
                        <h3>AWGN SNR Levels (dB)</h3>
                        <div class="checkbox-grid four-col">
                            <div class="checkbox-item"><input type="checkbox" id="snr-30" checked><label for="snr-30">30</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="snr-25" checked><label for="snr-25">25</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="snr-20" checked><label for="snr-20">20</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="snr-15" checked><label for="snr-15">15</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="snr-12"><label for="snr-12">12</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="snr-10"><label for="snr-10">10</label></div>
                        </div>
                        <h3>Multipath Delays (samples @ 48kHz)</h3>
                        <div class="checkbox-grid four-col">
                            <div class="checkbox-item"><input type="checkbox" id="mp-10"><label for="mp-10">10</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mp-20" checked><label for="mp-20">20</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mp-30" checked><label for="mp-30">30</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mp-48" checked><label for="mp-48">48</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="mp-60"><label for="mp-60">60</label></div>
                        </div>
                        <h3>Frequency Offsets (Hz)</h3>
                        <div class="checkbox-grid four-col">
                            <div class="checkbox-item"><input type="checkbox" id="foff-05"><label for="foff-05">0.5</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="foff-1" checked><label for="foff-1">1.0</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="foff-2" checked><label for="foff-2">2.0</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="foff-5" checked><label for="foff-5">5.0</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="foff-10"><label for="foff-10">10.0</label></div>
                        </div>
                    </div>
                    
                    <!-- Output Options -->
                    <div class="control-section">
                        <h2>Output Options</h2>
                        <div class="checkbox-grid">
                            <div class="checkbox-item"><input type="checkbox" id="opt-report" checked><label for="opt-report">Generate Report</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="opt-csv" checked><label for="opt-csv">Export CSV</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="opt-verbose"><label for="opt-verbose">Verbose Output</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="opt-savepcm"><label for="opt-savepcm">Save PCM Files</label></div>
                        </div>
                    </div>
                    
                    <!-- Action Buttons -->
                    <div class="action-bar">
                        <button class="btn btn-primary" id="btn-start" onclick="startTest()">▶ Start Test</button>
                        <button class="btn btn-danger" id="btn-stop" onclick="stopTest()" style="display:none;">■ Stop</button>
                        <button class="btn btn-secondary" onclick="resetConfig()">Reset</button>
                    </div>
                </div>
                
                <!-- RIGHT: Results Panel -->
                <div class="results-panel">
                    <!-- Status Bar -->
                    <div class="status-bar">
                        <div class="status-indicator">
                            <div class="status-dot" id="status-dot"></div>
                            <span id="status-text">Ready</span>
                            <span class="backend-indicator backend-pn" id="backend-badge">PhoenixNest</span>
                        </div>
                        <div class="status-metrics">
                            <div class="metric"><div class="metric-value" id="metric-tests">0</div><div class="metric-label">Tests</div></div>
                            <div class="metric"><div class="metric-value" id="metric-passed">0</div><div class="metric-label">Passed</div></div>
                            <div class="metric"><div class="metric-value" id="metric-rate">—</div><div class="metric-label">Pass Rate</div></div>
                            <div class="metric"><div class="metric-value" id="metric-ber">—</div><div class="metric-label">Avg BER</div></div>
                        </div>
                    </div>
                    
                    <!-- Progress -->
                    <div class="progress-container">
                        <div class="progress-header">
                            <span class="progress-label">Current: <span id="current-test">—</span></span>
                            <span class="progress-value"><span id="progress-pct">0</span>%</span>
                        </div>
                        <div class="progress-bar"><div class="progress-fill" id="progress-fill"></div></div>
                        <div class="progress-details">
                            <span>Elapsed: <span id="elapsed-time">0:00</span></span>
                            <span>Remaining: <span id="remaining-time">—</span></span>
                            <span>Rating: <span id="test-rating">—</span></span>
                        </div>
                    </div>
                    
                    <!-- Results Tables -->
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px;">
                        <div class="results-table-container">
                            <table class="results-table">
                                <thead><tr><th>Mode</th><th class="num">Passed</th><th class="num">Failed</th><th class="num">Total</th><th class="num">Rate</th></tr></thead>
                                <tbody id="modes-body"></tbody>
                            </table>
                        </div>
                        <div class="results-table-container">
                            <table class="results-table">
                                <thead><tr><th>Channel</th><th class="num">Passed</th><th class="num">Failed</th><th class="num">Total</th><th class="num">Rate</th></tr></thead>
                                <tbody id="results-body"></tbody>
                            </table>
                        </div>
                    </div>
                    
                    <!-- Totals Row -->
                    <div class="results-table-container">
                        <table class="results-table">
                            <tbody><tr style="background: #1a1a2a; font-weight: bold;"><td>TOTAL</td><td class="num pass" id="total-passed">—</td><td class="num fail" id="total-failed">—</td><td class="num" id="total-tests">—</td><td class="num rate" id="total-rate">—</td><td class="num" id="total-ber">—</td></tr></tbody>
                        </table>
                    </div>
                    
                    <!-- Output Log -->
                    <div class="output-container">
                        <div class="output-header">
                            <span class="output-title">Live Output</span>
                            <div><button class="output-btn" onclick="clearOutput()">Clear</button> <button class="output-btn" onclick="copyOutput()">Copy</button></div>
                        </div>
                        <div class="output-content" id="output">
<span class="output-line header">==============================================</span>
<span class="output-line header">M110A Modem Test Suite</span>
<span class="output-line header">==============================================</span>
<span class="output-line">Ready to run tests.</span>
<span class="output-line">Select backend and click Start.</span>
                        </div>
                    </div>
                    
                    <!-- Export Bar -->
                    <div class="export-bar">
                        <span class="export-label">Export results:</span>
                        <div class="export-buttons">
                            <button class="btn btn-secondary" onclick="exportReport()">📄 Markdown</button>
                            <button class="btn btn-secondary" onclick="exportCSV()">📊 CSV</button>
                            <button class="btn btn-secondary" onclick="exportJSON()">{ } JSON</button>
                        </div>
                    </div>
                </div>
            </div>
        </div>
)HTML";

// ============================================================
// RUN TESTS TAB JAVASCRIPT
// ============================================================
const char* HTML_JS_TESTS = R"JS(
        let testRunning = false;
        let currentBackend = 'phoenixnest';
        let testStartTime = null;
        let testDurationSec = 0;
        let channelResults = {};
        let modeResults = {};
        
        // Backend selection
        function selectBackend(backend) {
            currentBackend = backend;
            document.querySelectorAll('#tab-tests .radio-item').forEach(i => i.classList.remove('selected'));
            document.getElementById('backend-' + backend).checked = true;
            document.getElementById('backend-' + backend).closest('.radio-item').classList.add('selected');
            
            // Update badge
            const badge = document.getElementById('backend-badge');
            if (backend === 'brain') {
                badge.className = 'backend-indicator backend-brain';
                badge.textContent = 'Brain';
            } else {
                badge.className = 'backend-indicator backend-pn';
                badge.textContent = 'PhoenixNest';
            }
        }
        
        // Mode selection
        function selectModes(which) {
            document.querySelectorAll('[id^="mode-"]').forEach(cb => {
                if (which === 'all') cb.checked = true;
                else if (which === 'none') cb.checked = false;
                else if (which === 'short') cb.checked = cb.id.endsWith('s');
                else if (which === 'long') cb.checked = cb.id.endsWith('l');
                else if (which === 'fast') cb.checked = ['mode-600s','mode-600l','mode-1200s','mode-1200l','mode-2400s','mode-2400l'].includes(cb.id);
            });
        }
        
        // Presets
        function loadPreset() {
            const p = document.getElementById('test-preset').value;
            const presets = { quick: 1, standard: 3, extended: 10, overnight: 60 };
            document.getElementById('duration').value = presets[p] || 3;
            if (p === 'quick') selectModes('fast');
            else selectModes('all');
        }
        
        function getSelectedModes() {
            const modes = [];
            document.querySelectorAll('[id^="mode-"]:checked').forEach(cb => {
                modes.push(cb.id.replace('mode-', '').toUpperCase());
            });
            return modes.length === 12 ? 'all' : modes.join(',');
        }
        
        function getDurationSeconds() {
            const value = parseInt(document.getElementById('duration').value) || 3;
            const unit = document.getElementById('duration-unit').value;
            if (unit === 'sec') return value;
            if (unit === 'hr') return value * 3600;
            return value * 60;
        }
        
        // Start test
        async function startTest() {
            if (testRunning) return;
            testRunning = true;
            testStartTime = Date.now();
            testDurationSec = getDurationSeconds();
            
            document.getElementById('btn-start').style.display = 'none';
            document.getElementById('btn-stop').style.display = 'inline-block';
            document.getElementById('status-dot').className = 'status-dot running';
            document.getElementById('status-text').textContent = 'Running...';
            
            // Reset metrics
            document.getElementById('metric-tests').textContent = '0';
            document.getElementById('metric-passed').textContent = '0';
            document.getElementById('metric-rate').textContent = '—';
            document.getElementById('metric-ber').textContent = '—';
            document.getElementById('progress-fill').style.width = '0%';
            document.getElementById('progress-pct').textContent = '0';
            document.getElementById('test-rating').textContent = '—';
            
            clearOutput();
            channelResults = {};
            modeResults = {};
            document.getElementById('results-body').innerHTML = '';
            document.getElementById('modes-body').innerHTML = '';
            
            const backendName = currentBackend === 'brain' ? 'Brain' : 'PhoenixNest';
            addOutput('==============================================', 'header');
            addOutput('M110A ' + backendName + ' Exhaustive Test', 'header');
            addOutput('==============================================', 'header');
            addOutput('Starting tests...', 'info');
            
            const durationSec = getDurationSeconds();
            const modes = getSelectedModes();
            
            const params = new URLSearchParams();
            params.set('duration', durationSec.toString());
            params.set('backend', currentBackend);
            if (modes !== 'all') {
                params.set('modes', modes);
            }
            
            try {
                const response = await fetch('/run-exhaustive?' + params.toString());
                const reader = response.body.getReader();
                const decoder = new TextDecoder();
                
                while (testRunning) {
                    const { value, done } = await reader.read();
                    if (done) break;
                    
                    const text = decoder.decode(value);
                    for (const line of text.split('\n')) {
                        if (line.startsWith('data: ')) {
                            try {
                                const data = JSON.parse(line.substring(6));
                                handleTestEvent(data);
                            } catch (e) {}
                        }
                    }
                }
            } catch (err) {
                addOutput('Error: ' + err.message, 'error');
            }
            
            finishTest();
        }
        
        function handleTestEvent(data) {
            if (data.output) {
                let type = data.type || 'info';
                if (data.output.includes('PASS')) type = 'pass';
                else if (data.output.includes('FAIL')) type = 'fail';
                else if (data.output.includes('ERROR')) type = 'error';
                else if (data.output.includes('===')) type = 'header';
                addOutput(data.output, type);
            }
            
            if (data.tests !== undefined) {
                document.getElementById('metric-tests').textContent = data.tests;
                document.getElementById('total-tests').textContent = data.tests;
            }
            if (data.passed !== undefined) {
                document.getElementById('metric-passed').textContent = data.passed;
                document.getElementById('total-passed').textContent = data.passed;
            }
            if (data.failed !== undefined) {
                document.getElementById('total-failed').textContent = data.failed;
            }
            if (data.rate !== undefined) {
                const rateStr = data.rate.toFixed(1) + '%';
                document.getElementById('metric-rate').textContent = rateStr;
                document.getElementById('total-rate').textContent = rateStr;
            }
            if (data.ber !== undefined) {
                const ber = parseFloat(data.ber);
                if (!isNaN(ber)) {
                    const berStr = ber < 0.0001 ? ber.toExponential(2) : ber.toFixed(6);
                    document.getElementById('metric-ber').textContent = berStr;
                }
            }
            if (data.avgBer !== undefined) {
                const avgBer = parseFloat(data.avgBer);
                if (!isNaN(avgBer)) {
                    const berStr = avgBer < 0.0001 ? avgBer.toExponential(2) : avgBer.toFixed(6);
                    document.getElementById('metric-ber').textContent = berStr;
                    document.getElementById('total-ber').textContent = berStr;
                }
            }
            
            if (data.currentTest) {
                document.getElementById('current-test').textContent = data.currentTest;
            }
            
            if (data.progress !== undefined) {
                document.getElementById('progress-fill').style.width = data.progress + '%';
                document.getElementById('progress-pct').textContent = Math.round(data.progress);
            } else if (testStartTime && testDurationSec > 0) {
                const elapsed = (Date.now() - testStartTime) / 1000;
                const progress = Math.min(100, (elapsed / testDurationSec) * 100);
                document.getElementById('progress-fill').style.width = progress + '%';
                document.getElementById('progress-pct').textContent = Math.round(progress);
            }
            
            if (data.elapsed) {
                document.getElementById('elapsed-time').textContent = data.elapsed;
            } else if (testStartTime) {
                const elapsed = Math.floor((Date.now() - testStartTime) / 1000);
                const mins = Math.floor(elapsed / 60);
                const secs = elapsed % 60;
                document.getElementById('elapsed-time').textContent = mins + ':' + secs.toString().padStart(2, '0');
            }
            
            if (data.remaining) {
                document.getElementById('remaining-time').textContent = data.remaining;
            } else if (testStartTime && testDurationSec > 0) {
                const elapsed = (Date.now() - testStartTime) / 1000;
                const remaining = Math.max(0, testDurationSec - elapsed);
                const mins = Math.floor(remaining / 60);
                const secs = Math.floor(remaining % 60);
                document.getElementById('remaining-time').textContent = mins + ':' + secs.toString().padStart(2, '0');
            }
            
            if (data.rating) {
                const ratingEl = document.getElementById('test-rating');
                ratingEl.textContent = data.rating;
                if (data.rating === 'EXCELLENT') ratingEl.style.color = '#00ff88';
                else if (data.rating === 'GOOD') ratingEl.style.color = '#00d4ff';
                else if (data.rating === 'FAIR') ratingEl.style.color = '#ffaa00';
                else ratingEl.style.color = '#ff3a50';
            }
            
            if (data.type === 'mode_stat' && data.mode) {
                updateModeRow(data.mode, data.passed, data.failed, data.total, data.rate);
            }
            
            if (data.type === 'channel_stat' && data.channel) {
                updateChannelRow(data.channel, data.passed, data.failed, data.total, data.rate, data.avgBer);
            }
            
            if (data.currentTest && data.ber !== undefined) {
                const match = data.currentTest.match(/^(\S+) \+ (\S+)$/);
                if (match) {
                    const mode = match[1];
                    const channel = match[2];
                    const passed = data.output && data.output.includes('PASS');
                    accumulateModeResult(mode, passed);
                    accumulateChannelResult(channel, passed, data.ber);
                }
            }
            
            if (data.done) {
                testRunning = false;
            }
        }
        
        function accumulateModeResult(mode, passed) {
            if (!modeResults[mode]) {
                modeResults[mode] = { passed: 0, failed: 0, total: 0, rate: 0 };
            }
            const r = modeResults[mode];
            r.total++;
            if (passed) r.passed++;
            else r.failed++;
            r.rate = (r.passed / r.total) * 100;
            rebuildModeTable();
        }
        
        function updateModeRow(mode, passed, failed, total, rate) {
            modeResults[mode] = { passed, failed, total, rate };
            rebuildModeTable();
        }
        
        function rebuildModeTable() {
            const tbody = document.getElementById('modes-body');
            tbody.innerHTML = '';
            const sortOrder = ['75S', '75L', '150S', '150L', '300S', '300L', '600S', '600L', '1200S', '1200L', '2400S', '2400L'];
            const modes = Object.keys(modeResults).sort((a, b) => {
                const ia = sortOrder.indexOf(a);
                const ib = sortOrder.indexOf(b);
                if (ia === -1 && ib === -1) return a.localeCompare(b);
                if (ia === -1) return 1;
                if (ib === -1) return -1;
                return ia - ib;
            });
            for (const m of modes) {
                const r = modeResults[m];
                const rateClass = r.rate >= 80 ? 'pass' : r.rate >= 50 ? 'rate' : 'fail';
                const row = document.createElement('tr');
                row.innerHTML = '<td>' + m + '</td><td class="num pass">' + r.passed + '</td><td class="num fail">' + r.failed + '</td><td class="num">' + r.total + '</td><td class="num ' + rateClass + '">' + r.rate.toFixed(1) + '%</td>';
                tbody.appendChild(row);
            }
        }
        
        function accumulateChannelResult(channel, passed, ber) {
            if (!channelResults[channel]) {
                channelResults[channel] = { passed: 0, failed: 0, total: 0, rate: 0, totalBer: 0, avgBer: 0 };
            }
            const r = channelResults[channel];
            r.total++;
            if (passed) r.passed++;
            else r.failed++;
            r.totalBer += ber;
            r.avgBer = r.totalBer / r.total;
            r.rate = (r.passed / r.total) * 100;
            rebuildChannelTable();
        }
        
        function updateChannelRow(channel, passed, failed, total, rate, avgBer) {
            channelResults[channel] = { passed, failed, total, rate, avgBer };
            rebuildChannelTable();
        }
        
        function rebuildChannelTable() {
            const tbody = document.getElementById('results-body');
            tbody.innerHTML = '';
            const sortOrder = ['clean', 'awgn_30db', 'awgn_25db', 'awgn_20db', 'awgn_15db', 'mp_24samp', 'mp_48samp', 'foff_1hz', 'foff_5hz', 'moderate_hf', 'poor_hf'];
            const channels = Object.keys(channelResults).sort((a, b) => {
                const ia = sortOrder.indexOf(a);
                const ib = sortOrder.indexOf(b);
                if (ia === -1 && ib === -1) return a.localeCompare(b);
                if (ia === -1) return 1;
                if (ib === -1) return -1;
                return ia - ib;
            });
            for (const ch of channels) {
                const r = channelResults[ch];
                const rateClass = r.rate >= 80 ? 'pass' : r.rate >= 50 ? 'rate' : 'fail';
                const berStr = r.avgBer < 0.0001 ? r.avgBer.toExponential(2) : r.avgBer.toFixed(6);
                const row = document.createElement('tr');
                row.innerHTML = '<td>' + ch + '</td><td class="num pass">' + r.passed + '</td><td class="num fail">' + r.failed + '</td><td class="num">' + r.total + '</td><td class="num ' + rateClass + '">' + r.rate.toFixed(1) + '%</td><td class="num">' + berStr + '</td>';
                tbody.appendChild(row);
            }
        }
        
        function finishTest() {
            testRunning = false;
            document.getElementById('btn-start').style.display = 'inline-block';
            document.getElementById('btn-stop').style.display = 'none';
            
            const passRate = parseFloat(document.getElementById('metric-rate').textContent) || 0;
            if (passRate >= 95) {
                document.getElementById('status-dot').className = 'status-dot pass';
                document.getElementById('status-text').textContent = 'Complete - EXCELLENT';
            } else if (passRate >= 80) {
                document.getElementById('status-dot').className = 'status-dot pass';
                document.getElementById('status-text').textContent = 'Complete - GOOD';
            } else {
                document.getElementById('status-dot').className = 'status-dot fail';
                document.getElementById('status-text').textContent = 'Complete - Needs Work';
            }
            
            document.getElementById('progress-fill').style.width = '100%';
            document.getElementById('progress-pct').textContent = '100';
            document.getElementById('current-test').textContent = 'Done';
        }
        
        function stopTest() {
            testRunning = false;
            fetch('/stop-test');
            addOutput('Test stopped by user', 'warning');
            finishTest();
        }
        
        function resetConfig() {
            document.getElementById('test-preset').value = 'standard';
            loadPreset();
            selectBackend('phoenixnest');
        }
)JS";

} // namespace test_gui
