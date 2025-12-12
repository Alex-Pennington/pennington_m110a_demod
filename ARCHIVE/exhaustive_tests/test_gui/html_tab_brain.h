#pragma once
/**
 * @file html_tab_brain.h
 * @brief Brain Tests tab HTML/JS for M110A Test GUI
 * 
 * Dedicated tab for Brain (Charles Brain G4GUO) modem core testing
 */

namespace test_gui {

// ============================================================
// BRAIN TESTS TAB HTML
// ============================================================
const char* HTML_TAB_BRAIN = R"HTML(
        <!-- ============ BRAIN TESTS TAB ============ -->
        <div id="tab-brain" class="tab-content">
            <div class="test-layout">
                <!-- LEFT: Control Panel -->
                <div class="control-panel">
                    
                    <!-- Backend Info -->
                    <div class="control-section">
                        <h2>🧠 Brain Modem Core</h2>
                        <p style="color: #888; font-size: 12px; margin: 0;">Charles Brain (G4GUO) reference implementation (brain_exhaustive_test.exe)</p>
                    </div>
                    
                    <!-- Test Configuration -->
                    <div class="control-section">
                        <h2>Test Configuration</h2>
                        <div class="form-row">
                            <div class="form-group">
                                <label>Preset</label>
                                <select id="br-test-preset" onchange="brLoadPreset()">
                                    <option value="quick">Quick Smoke Test (1 min)</option>
                                    <option value="standard" selected>Standard Test (3 min)</option>
                                    <option value="extended">Extended Test (10 min)</option>
                                    <option value="overnight">Overnight Soak (60 min)</option>
                                </select>
                            </div>
                        </div>
                        <div class="form-row">
                            <div class="form-group narrow"><label>Duration</label><input type="number" id="br-duration" value="3" min="1"></div>
                            <div class="form-group narrow">
                                <label>Unit</label>
                                <select id="br-duration-unit">
                                    <option value="min" selected>Minutes</option>
                                    <option value="sec">Seconds</option>
                                    <option value="hr">Hours</option>
                                </select>
                            </div>
                        </div>
                    </div>
                    
                    <!-- Mode Selection -->
                    <div class="control-section">
                        <h2>Modes</h2>
                        <div class="quick-select">
                            <button class="quick-btn" onclick="brSelectModes('all')">All</button>
                            <button class="quick-btn" onclick="brSelectModes('none')">None</button>
                            <button class="quick-btn" onclick="brSelectModes('short')">Short</button>
                            <button class="quick-btn" onclick="brSelectModes('long')">Long</button>
                            <button class="quick-btn" onclick="brSelectModes('fast')">Fast Only</button>
                        </div>
                        <div class="checkbox-grid three-col">
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-75s" checked><label for="br-mode-75s">75S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-75l" checked><label for="br-mode-75l">75L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-150s" checked><label for="br-mode-150s">150S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-150l" checked><label for="br-mode-150l">150L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-300s" checked><label for="br-mode-300s">300S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-300l" checked><label for="br-mode-300l">300L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-600s" checked><label for="br-mode-600s">600S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-600l" checked><label for="br-mode-600l">600L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-1200s" checked><label for="br-mode-1200s">1200S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-1200l" checked><label for="br-mode-1200l">1200L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-2400s" checked><label for="br-mode-2400s">2400S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="br-mode-2400l" checked><label for="br-mode-2400l">2400L</label></div>
                        </div>
                    </div>
                    
                    <!-- Output Options -->
                    <div class="control-section">
                        <h2>Output Options</h2>
                        <div class="checkbox-grid">
                            <div class="checkbox-item"><input type="checkbox" id="br-opt-report" checked><label for="br-opt-report">Generate Report</label></div>
                        </div>
                    </div>
                    
                    <!-- Action Buttons -->
                    <div class="action-bar">
                        <button class="btn btn-primary" id="br-btn-start" onclick="brStartTest()">▶ Start Test</button>
                        <button class="btn btn-danger" id="br-btn-stop" onclick="brStopTest()" style="display:none;">■ Stop</button>
                        <button class="btn btn-secondary" onclick="brResetConfig()">Reset</button>
                    </div>
                </div>
                
                <!-- RIGHT: Results Panel -->
                <div class="results-panel">
                    <!-- Status Bar -->
                    <div class="status-bar">
                        <div class="status-indicator">
                            <div class="status-dot" id="br-status-dot"></div>
                            <span id="br-status-text">Ready</span>
                            <span class="backend-indicator backend-brain">Brain</span>
                        </div>
                        <div class="status-metrics">
                            <div class="metric"><div class="metric-value" id="br-metric-tests">0</div><div class="metric-label">Tests</div></div>
                            <div class="metric"><div class="metric-value" id="br-metric-passed">0</div><div class="metric-label">Passed</div></div>
                            <div class="metric"><div class="metric-value" id="br-metric-rate">—</div><div class="metric-label">Pass Rate</div></div>
                            <div class="metric"><div class="metric-value" id="br-metric-ber">—</div><div class="metric-label">Avg BER</div></div>
                        </div>
                    </div>
                    
                    <!-- Progress -->
                    <div class="progress-container">
                        <div class="progress-header">
                            <span class="progress-label">Current: <span id="br-current-test">—</span></span>
                            <span class="progress-value"><span id="br-progress-pct">0</span>%</span>
                        </div>
                        <div class="progress-bar"><div class="progress-fill" id="br-progress-fill"></div></div>
                        <div class="progress-details">
                            <span>Elapsed: <span id="br-elapsed-time">0:00</span></span>
                            <span>Remaining: <span id="br-remaining-time">—</span></span>
                            <span>Rating: <span id="br-test-rating">—</span></span>
                        </div>
                    </div>
                    
                    <!-- Results Tables -->
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px;">
                        <div class="results-table-container">
                            <table class="results-table">
                                <thead><tr><th>Mode</th><th class="num">Passed</th><th class="num">Failed</th><th class="num">Total</th><th class="num">Rate</th></tr></thead>
                                <tbody id="br-modes-body"></tbody>
                            </table>
                        </div>
                        <div class="results-table-container">
                            <table class="results-table">
                                <thead><tr><th>Channel</th><th class="num">Passed</th><th class="num">Failed</th><th class="num">Total</th><th class="num">Rate</th></tr></thead>
                                <tbody id="br-results-body"></tbody>
                            </table>
                        </div>
                    </div>
                    
                    <!-- Totals Row -->
                    <div class="results-table-container">
                        <table class="results-table">
                            <tbody><tr style="background: #1a1a2a; font-weight: bold;"><td>TOTAL</td><td class="num pass" id="br-total-passed">—</td><td class="num fail" id="br-total-failed">—</td><td class="num" id="br-total-tests">—</td><td class="num rate" id="br-total-rate">—</td><td class="num" id="br-total-ber">—</td></tr></tbody>
                        </table>
                    </div>
                    
                    <!-- Output Log -->
                    <div class="output-container">
                        <div class="output-header">
                            <span class="output-title">Live Output</span>
                            <div><button class="output-btn" onclick="brClearOutput()">Clear</button> <button class="output-btn" onclick="brCopyOutput()">Copy</button></div>
                        </div>
                        <div class="output-content" id="br-output">
<span class="output-line header">==============================================</span>
<span class="output-line header">Brain M110A Test Suite</span>
<span class="output-line header">==============================================</span>
<span class="output-line">Ready to run tests.</span>
<span class="output-line">Configure options and click Start.</span>
                        </div>
                    </div>
                    
                    <!-- Export Bar -->
                    <div class="export-bar">
                        <span class="export-label">Export results:</span>
                        <div class="export-buttons">
                            <button class="btn btn-secondary" onclick="brExportReport()">📄 Markdown</button>
                            <button class="btn btn-secondary" onclick="brExportCSV()">📊 CSV</button>
                            <button class="btn btn-secondary" onclick="brExportJSON()">{ } JSON</button>
                        </div>
                    </div>
                </div>
            </div>
        </div>
)HTML";

// ============================================================
// BRAIN TESTS TAB JAVASCRIPT
// ============================================================
const char* HTML_JS_BRAIN = R"JS(
        // Brain-specific state
        let brTestRunning = false;
        let brTestStartTime = null;
        let brTestDurationSec = 0;
        let brChannelResults = {};
        let brModeResults = {};
        
        // Mode selection
        function brSelectModes(which) {
            document.querySelectorAll('[id^="br-mode-"]').forEach(cb => {
                if (which === 'all') cb.checked = true;
                else if (which === 'none') cb.checked = false;
                else if (which === 'short') cb.checked = cb.id.endsWith('s');
                else if (which === 'long') cb.checked = cb.id.endsWith('l');
                else if (which === 'fast') cb.checked = ['br-mode-600s','br-mode-600l','br-mode-1200s','br-mode-1200l','br-mode-2400s','br-mode-2400l'].includes(cb.id);
            });
        }
        
        // Presets
        function brLoadPreset() {
            const p = document.getElementById('br-test-preset').value;
            const presets = { quick: 1, standard: 3, extended: 10, overnight: 60 };
            document.getElementById('br-duration').value = presets[p] || 3;
            if (p === 'quick') brSelectModes('fast');
            else brSelectModes('all');
        }
        
        function brGetSelectedModes() {
            const modes = [];
            document.querySelectorAll('[id^="br-mode-"]:checked').forEach(cb => {
                modes.push(cb.id.replace('br-mode-', '').toUpperCase());
            });
            return modes.length === 12 ? '' : modes.join(',');
        }
        
        function brGetDurationSeconds() {
            const value = parseInt(document.getElementById('br-duration').value) || 3;
            const unit = document.getElementById('br-duration-unit').value;
            if (unit === 'sec') return value;
            if (unit === 'hr') return value * 3600;
            return value * 60;
        }
        
        // Build command line args
        function brBuildArgs() {
            const args = [];
            
            // Duration
            args.push('--duration', brGetDurationSeconds().toString());
            
            // Modes
            const modes = brGetSelectedModes();
            if (modes) args.push('--modes', modes);
            
            return args;
        }
        
        // Start test
        async function brStartTest() {
            if (brTestRunning) return;
            brTestRunning = true;
            brTestStartTime = Date.now();
            brTestDurationSec = brGetDurationSeconds();
            
            document.getElementById('br-btn-start').style.display = 'none';
            document.getElementById('br-btn-stop').style.display = 'inline-block';
            document.getElementById('br-status-dot').className = 'status-dot running';
            document.getElementById('br-status-text').textContent = 'Running...';
            
            // Reset metrics
            document.getElementById('br-metric-tests').textContent = '0';
            document.getElementById('br-metric-passed').textContent = '0';
            document.getElementById('br-metric-rate').textContent = '—';
            document.getElementById('br-metric-ber').textContent = '—';
            document.getElementById('br-progress-fill').style.width = '0%';
            document.getElementById('br-progress-pct').textContent = '0';
            document.getElementById('br-test-rating').textContent = '—';
            
            brClearOutput();
            brChannelResults = {};
            brModeResults = {};
            document.getElementById('br-results-body').innerHTML = '';
            document.getElementById('br-modes-body').innerHTML = '';
            
            brAddOutput('==============================================', 'header');
            brAddOutput('Brain Modem Exhaustive Test', 'header');
            brAddOutput('==============================================', 'header');
            
            const args = brBuildArgs();
            brAddOutput('Command: brain_exhaustive_test.exe ' + args.join(' '), 'info');
            brAddOutput('Starting tests...', 'info');
            
            const params = new URLSearchParams();
            params.set('backend', 'brain');
            params.set('args', JSON.stringify(args));
            
            try {
                const response = await fetch('/run-exhaustive?' + params.toString());
                const reader = response.body.getReader();
                const decoder = new TextDecoder();
                
                while (brTestRunning) {
                    const { value, done } = await reader.read();
                    if (done) break;
                    
                    const text = decoder.decode(value);
                    for (const line of text.split('\n')) {
                        if (line.startsWith('data: ')) {
                            try {
                                const data = JSON.parse(line.substring(6));
                                brHandleTestEvent(data);
                            } catch (e) {}
                        }
                    }
                }
            } catch (err) {
                brAddOutput('Error: ' + err.message, 'error');
            }
            
            brFinishTest();
        }
        
        function brHandleTestEvent(data) {
            if (data.output) {
                let type = data.type || 'info';
                if (data.output.includes('PASS')) type = 'pass';
                else if (data.output.includes('FAIL')) type = 'fail';
                else if (data.output.includes('ERROR')) type = 'error';
                else if (data.output.includes('===')) type = 'header';
                brAddOutput(data.output, type);
            }
            
            if (data.tests !== undefined) {
                document.getElementById('br-metric-tests').textContent = data.tests;
                document.getElementById('br-total-tests').textContent = data.tests;
            }
            if (data.passed !== undefined) {
                document.getElementById('br-metric-passed').textContent = data.passed;
                document.getElementById('br-total-passed').textContent = data.passed;
            }
            if (data.failed !== undefined) {
                document.getElementById('br-total-failed').textContent = data.failed;
            }
            if (data.rate !== undefined) {
                const rateStr = data.rate.toFixed(1) + '%';
                document.getElementById('br-metric-rate').textContent = rateStr;
                document.getElementById('br-total-rate').textContent = rateStr;
            }
            if (data.ber !== undefined || data.avgBer !== undefined) {
                const ber = parseFloat(data.avgBer || data.ber);
                if (!isNaN(ber)) {
                    const berStr = ber < 0.0001 ? ber.toExponential(2) : ber.toFixed(6);
                    document.getElementById('br-metric-ber').textContent = berStr;
                    document.getElementById('br-total-ber').textContent = berStr;
                }
            }
            
            if (data.currentTest) {
                document.getElementById('br-current-test').textContent = data.currentTest;
            }
            
            // Update progress
            if (data.progress !== undefined) {
                document.getElementById('br-progress-fill').style.width = data.progress + '%';
                document.getElementById('br-progress-pct').textContent = Math.round(data.progress);
            } else if (brTestStartTime && brTestDurationSec > 0) {
                const elapsed = (Date.now() - brTestStartTime) / 1000;
                const progress = Math.min(100, (elapsed / brTestDurationSec) * 100);
                document.getElementById('br-progress-fill').style.width = progress + '%';
                document.getElementById('br-progress-pct').textContent = Math.round(progress);
            }
            
            // Update elapsed/remaining time
            if (brTestStartTime) {
                const elapsed = Math.floor((Date.now() - brTestStartTime) / 1000);
                const mins = Math.floor(elapsed / 60);
                const secs = elapsed % 60;
                document.getElementById('br-elapsed-time').textContent = mins + ':' + secs.toString().padStart(2, '0');
                
                if (brTestDurationSec > 0) {
                    const remaining = Math.max(0, brTestDurationSec - elapsed);
                    const rmins = Math.floor(remaining / 60);
                    const rsecs = Math.floor(remaining % 60);
                    document.getElementById('br-remaining-time').textContent = rmins + ':' + rsecs.toString().padStart(2, '0');
                }
            }
            
            if (data.rating) {
                const ratingEl = document.getElementById('br-test-rating');
                ratingEl.textContent = data.rating;
                if (data.rating === 'EXCELLENT') ratingEl.style.color = '#00ff88';
                else if (data.rating === 'GOOD') ratingEl.style.color = '#00d4ff';
                else if (data.rating === 'FAIR') ratingEl.style.color = '#ffaa00';
                else ratingEl.style.color = '#ff3a50';
            }
            
            if (data.type === 'mode_stat' && data.mode) {
                brModeResults[data.mode] = { passed: data.passed, failed: data.failed, total: data.total, rate: data.rate };
                brRebuildModeTable();
            }
            
            if (data.type === 'channel_stat' && data.channel) {
                brChannelResults[data.channel] = { passed: data.passed, failed: data.failed, total: data.total, rate: data.rate, avgBer: data.avgBer };
                brRebuildChannelTable();
            }
            
            // Accumulate results from individual test events
            if (data.currentTest && data.output) {
                const match = data.currentTest.match(/^(\S+) \+ (\S+)$/);
                if (match) {
                    const mode = match[1];
                    const channel = match[2];
                    const passed = data.output.includes('PASS');
                    const ber = data.ber !== undefined ? parseFloat(data.ber) : 0;
                    
                    // Accumulate mode stats
                    if (!brModeResults[mode]) {
                        brModeResults[mode] = { passed: 0, failed: 0, total: 0, rate: 0 };
                    }
                    brModeResults[mode].total++;
                    if (passed) brModeResults[mode].passed++;
                    else brModeResults[mode].failed++;
                    brModeResults[mode].rate = (brModeResults[mode].passed / brModeResults[mode].total) * 100;
                    brRebuildModeTable();
                    
                    // Accumulate channel stats
                    if (!brChannelResults[channel]) {
                        brChannelResults[channel] = { passed: 0, failed: 0, total: 0, rate: 0, totalBer: 0, avgBer: 0 };
                    }
                    brChannelResults[channel].total++;
                    if (passed) brChannelResults[channel].passed++;
                    else brChannelResults[channel].failed++;
                    brChannelResults[channel].totalBer = (brChannelResults[channel].totalBer || 0) + ber;
                    brChannelResults[channel].avgBer = brChannelResults[channel].totalBer / brChannelResults[channel].total;
                    brChannelResults[channel].rate = (brChannelResults[channel].passed / brChannelResults[channel].total) * 100;
                    brRebuildChannelTable();
                }
            }
            
            if (data.done) {
                brTestRunning = false;
            }
        }
        
        function brRebuildModeTable() {
            const tbody = document.getElementById('br-modes-body');
            tbody.innerHTML = '';
            const sortOrder = ['75S', '75L', '150S', '150L', '300S', '300L', '600S', '600L', '1200S', '1200L', '2400S', '2400L'];
            const modes = Object.keys(brModeResults).sort((a, b) => {
                const ia = sortOrder.indexOf(a);
                const ib = sortOrder.indexOf(b);
                if (ia === -1 && ib === -1) return a.localeCompare(b);
                if (ia === -1) return 1;
                if (ib === -1) return -1;
                return ia - ib;
            });
            for (const m of modes) {
                const r = brModeResults[m];
                const rateClass = r.rate >= 80 ? 'pass' : r.rate >= 50 ? 'rate' : 'fail';
                const row = document.createElement('tr');
                row.innerHTML = '<td>' + m + '</td><td class="num pass">' + r.passed + '</td><td class="num fail">' + r.failed + '</td><td class="num">' + r.total + '</td><td class="num ' + rateClass + '">' + r.rate.toFixed(1) + '%</td>';
                tbody.appendChild(row);
            }
        }
        
        function brRebuildChannelTable() {
            const tbody = document.getElementById('br-results-body');
            tbody.innerHTML = '';
            const channels = Object.keys(brChannelResults);
            for (const ch of channels) {
                const r = brChannelResults[ch];
                const rateClass = r.rate >= 80 ? 'pass' : r.rate >= 50 ? 'rate' : 'fail';
                const row = document.createElement('tr');
                row.innerHTML = '<td>' + ch + '</td><td class="num pass">' + r.passed + '</td><td class="num fail">' + r.failed + '</td><td class="num">' + r.total + '</td><td class="num ' + rateClass + '">' + r.rate.toFixed(1) + '%</td>';
                tbody.appendChild(row);
            }
        }
        
        function brFinishTest() {
            brTestRunning = false;
            document.getElementById('br-btn-start').style.display = 'inline-block';
            document.getElementById('br-btn-stop').style.display = 'none';
            
            const passRate = parseFloat(document.getElementById('br-metric-rate').textContent) || 0;
            if (passRate >= 95) {
                document.getElementById('br-status-dot').className = 'status-dot pass';
                document.getElementById('br-status-text').textContent = 'Complete - EXCELLENT';
            } else if (passRate >= 80) {
                document.getElementById('br-status-dot').className = 'status-dot pass';
                document.getElementById('br-status-text').textContent = 'Complete - GOOD';
            } else {
                document.getElementById('br-status-dot').className = 'status-dot fail';
                document.getElementById('br-status-text').textContent = 'Complete - Needs Work';
            }
            
            document.getElementById('br-progress-fill').style.width = '100%';
            document.getElementById('br-progress-pct').textContent = '100';
            document.getElementById('br-current-test').textContent = 'Done';
        }
        
        function brStopTest() {
            brTestRunning = false;
            fetch('/stop-test');
            brAddOutput('Test stopped by user', 'warning');
            brFinishTest();
        }
        
        function brResetConfig() {
            document.getElementById('br-test-preset').value = 'standard';
            brLoadPreset();
        }
        
        function brAddOutput(text, type) {
            const output = document.getElementById('br-output');
            const line = document.createElement('span');
            line.className = 'output-line ' + (type || '');
            line.textContent = text;
            output.appendChild(line);
            output.scrollTop = output.scrollHeight;
        }
        
        function brClearOutput() {
            document.getElementById('br-output').innerHTML = '';
        }
        
        function brCopyOutput() {
            const text = document.getElementById('br-output').innerText;
            navigator.clipboard.writeText(text);
        }
        
        function brExportReport() {
            window.open('/export-report?backend=brain&format=md', '_blank');
        }
        
        function brExportCSV() {
            window.open('/export-report?backend=brain&format=csv', '_blank');
        }
        
        function brExportJSON() {
            window.open('/export-report?backend=brain&format=json', '_blank');
        }
)JS";

// CSS for Brain-specific styling
const char* HTML_CSS_BRAIN = R"CSS(
        .backend-brain {
            background: #6366f1 !important;
            color: white;
        }
)CSS";

} // namespace test_gui
