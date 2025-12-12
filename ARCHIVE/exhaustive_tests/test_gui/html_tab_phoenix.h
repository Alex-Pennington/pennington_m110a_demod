#pragma once
/**
 * @file html_tab_phoenix.h
 * @brief Phoenix Tests tab HTML/JS for M110A Test GUI
 * 
 * Dedicated tab for PhoenixNest modem core testing with all CLI options
 */

namespace test_gui {

// ============================================================
// PHOENIX TESTS TAB HTML
// ============================================================
const char* HTML_TAB_PHOENIX = R"HTML(
        <!-- ============ PHOENIX TESTS TAB ============ -->
        <div id="tab-phoenix" class="tab-content active">
            <div class="test-layout">
                <!-- LEFT: Control Panel -->
                <div class="control-panel">
                    
                    <!-- Backend Info -->
                    <div class="control-section">
                        <h2>🚀 PhoenixNest Core</h2>
                        <p style="color: #888; font-size: 12px; margin: 0;">Your M110A implementation (exhaustive_test.exe)</p>
                    </div>
                    
                    <!-- Test Configuration -->
                    <div class="control-section">
                        <h2>Test Configuration</h2>
                        <div class="form-row">
                            <div class="form-group">
                                <label>Preset</label>
                                <select id="pn-test-preset" onchange="pnLoadPreset()">
                                    <option value="quick">Quick Smoke Test (1 min)</option>
                                    <option value="standard" selected>Standard Test (3 min)</option>
                                    <option value="extended">Extended Test (10 min)</option>
                                    <option value="overnight">Overnight Soak (60 min)</option>
                                </select>
                            </div>
                        </div>
                        <div class="form-row">
                            <div class="form-group narrow"><label>Duration</label><input type="number" id="pn-duration" value="3" min="1"></div>
                            <div class="form-group narrow">
                                <label>Unit</label>
                                <select id="pn-duration-unit">
                                    <option value="min" selected>Minutes</option>
                                    <option value="sec">Seconds</option>
                                    <option value="hr">Hours</option>
                                </select>
                            </div>
                            <div class="form-group narrow"><label>Iterations</label><input type="number" id="pn-iterations" value="0" min="0" title="0 = use duration"></div>
                        </div>
                    </div>
                    
                    <!-- Mode Selection -->
                    <div class="control-section">
                        <h2>Modes</h2>
                        <div class="quick-select">
                            <button class="quick-btn" onclick="pnSelectModes('all')">All</button>
                            <button class="quick-btn" onclick="pnSelectModes('none')">None</button>
                            <button class="quick-btn" onclick="pnSelectModes('short')">Short</button>
                            <button class="quick-btn" onclick="pnSelectModes('long')">Long</button>
                            <button class="quick-btn" onclick="pnSelectModes('fast')">Fast Only</button>
                        </div>
                        <div class="checkbox-grid three-col">
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-75s" checked><label for="pn-mode-75s">75S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-75l" checked><label for="pn-mode-75l">75L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-150s" checked><label for="pn-mode-150s">150S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-150l" checked><label for="pn-mode-150l">150L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-300s" checked><label for="pn-mode-300s">300S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-300l" checked><label for="pn-mode-300l">300L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-600s" checked><label for="pn-mode-600s">600S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-600l" checked><label for="pn-mode-600l">600L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-1200s" checked><label for="pn-mode-1200s">1200S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-1200l" checked><label for="pn-mode-1200l">1200L</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-2400s" checked><label for="pn-mode-2400s">2400S</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-mode-2400l" checked><label for="pn-mode-2400l">2400L</label></div>
                        </div>
                    </div>
                    
                    <!-- Equalizer Options -->
                    <div class="control-section">
                        <h2>Equalizer</h2>
                        <div class="form-row">
                            <div class="form-group">
                                <label>Type</label>
                                <select id="pn-equalizer">
                                    <option value="DFE" selected>DFE (Default)</option>
                                    <option value="NONE">None</option>
                                    <option value="DFE_RLS">DFE RLS</option>
                                    <option value="MLSE_L2">MLSE L2</option>
                                    <option value="MLSE_L3">MLSE L3</option>
                                    <option value="MLSE_ADAPTIVE">MLSE Adaptive</option>
                                    <option value="TURBO">Turbo</option>
                                </select>
                            </div>
                        </div>
                        <div class="checkbox-grid">
                            <div class="checkbox-item"><input type="checkbox" id="pn-multi-eq"><label for="pn-multi-eq">Test Multiple EQs</label></div>
                        </div>
                        <div id="pn-eq-list" style="display:none; margin-top: 8px;">
                            <div class="checkbox-grid three-col">
                                <div class="checkbox-item"><input type="checkbox" id="pn-eq-none"><label for="pn-eq-none">NONE</label></div>
                                <div class="checkbox-item"><input type="checkbox" id="pn-eq-dfe" checked><label for="pn-eq-dfe">DFE</label></div>
                                <div class="checkbox-item"><input type="checkbox" id="pn-eq-dfe-rls"><label for="pn-eq-dfe-rls">DFE_RLS</label></div>
                                <div class="checkbox-item"><input type="checkbox" id="pn-eq-mlse-l2"><label for="pn-eq-mlse-l2">MLSE_L2</label></div>
                                <div class="checkbox-item"><input type="checkbox" id="pn-eq-mlse-l3"><label for="pn-eq-mlse-l3">MLSE_L3</label></div>
                                <div class="checkbox-item"><input type="checkbox" id="pn-eq-turbo"><label for="pn-eq-turbo">TURBO</label></div>
                            </div>
                        </div>
                    </div>
                    
                    <!-- AFC Options -->
                    <div class="control-section">
                        <h2>AFC Algorithm</h2>
                        <div class="form-row">
                            <div class="form-group">
                                <label>Type</label>
                                <select id="pn-afc">
                                    <option value="LEGACY" selected>LEGACY (Default)</option>
                                    <option value="MULTICHANNEL">MULTICHANNEL</option>
                                    <option value="EXTENDED">EXTENDED</option>
                                    <option value="MOOSE">MOOSE</option>
                                </select>
                            </div>
                        </div>
                    </div>
                    
                    <!-- Progressive Tests -->
                    <div class="control-section">
                        <h2>Progressive Tests</h2>
                        <div class="checkbox-grid">
                            <div class="checkbox-item"><input type="checkbox" id="pn-prog-snr"><label for="pn-prog-snr">SNR Sweep</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-prog-freq"><label for="pn-prog-freq">Freq Offset Sweep</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-prog-multipath"><label for="pn-prog-multipath">Multipath Sweep</label></div>
                        </div>
                    </div>
                    
                    <!-- Advanced Options -->
                    <div class="control-section">
                        <h2>Advanced Options</h2>
                        <div class="checkbox-grid">
                            <div class="checkbox-item"><input type="checkbox" id="pn-use-server"><label for="pn-use-server">Use TCP Server Backend</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-auto-detect"><label for="pn-auto-detect">Auto Mode Detection</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-reference"><label for="pn-reference">Reference Sample Test</label></div>
                        </div>
                        <div id="pn-server-opts" style="display:none; margin-top: 8px;">
                            <div class="form-row">
                                <div class="form-group"><label>Host</label><input type="text" id="pn-host" value="127.0.0.1"></div>
                                <div class="form-group narrow"><label>Port</label><input type="number" id="pn-port" value="4999"></div>
                            </div>
                        </div>
                        <div class="form-row">
                            <div class="form-group narrow"><label>Parallel Jobs</label><input type="number" id="pn-parallel" value="1" min="1" max="32"></div>
                        </div>
                    </div>
                    
                    <!-- Output Options -->
                    <div class="control-section">
                        <h2>Output Options</h2>
                        <div class="checkbox-grid">
                            <div class="checkbox-item"><input type="checkbox" id="pn-opt-report" checked><label for="pn-opt-report">Generate Report</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="pn-opt-csv"><label for="pn-opt-csv">Export CSV</label></div>
                        </div>
                    </div>
                    
                    <!-- Action Buttons -->
                    <div class="action-bar">
                        <button class="btn btn-primary" id="pn-btn-start" onclick="pnStartTest()">▶ Start Test</button>
                        <button class="btn btn-danger" id="pn-btn-stop" onclick="pnStopTest()" style="display:none;">■ Stop</button>
                        <button class="btn btn-secondary" onclick="pnResetConfig()">Reset</button>
                    </div>
                </div>
                
                <!-- RIGHT: Results Panel -->
                <div class="results-panel">
                    <!-- Status Bar -->
                    <div class="status-bar">
                        <div class="status-indicator">
                            <div class="status-dot" id="pn-status-dot"></div>
                            <span id="pn-status-text">Ready</span>
                            <span class="backend-indicator backend-pn">PhoenixNest</span>
                        </div>
                        <div class="status-metrics">
                            <div class="metric"><div class="metric-value" id="pn-metric-tests">0</div><div class="metric-label">Tests</div></div>
                            <div class="metric"><div class="metric-value" id="pn-metric-passed">0</div><div class="metric-label">Passed</div></div>
                            <div class="metric"><div class="metric-value" id="pn-metric-rate">—</div><div class="metric-label">Pass Rate</div></div>
                            <div class="metric"><div class="metric-value" id="pn-metric-ber">—</div><div class="metric-label">Avg BER</div></div>
                        </div>
                    </div>
                    
                    <!-- Progress -->
                    <div class="progress-container">
                        <div class="progress-header">
                            <span class="progress-label">Current: <span id="pn-current-test">—</span></span>
                            <span class="progress-value"><span id="pn-progress-pct">0</span>%</span>
                        </div>
                        <div class="progress-bar"><div class="progress-fill" id="pn-progress-fill"></div></div>
                        <div class="progress-details">
                            <span>Elapsed: <span id="pn-elapsed-time">0:00</span></span>
                            <span>Remaining: <span id="pn-remaining-time">—</span></span>
                            <span>Rating: <span id="pn-test-rating">—</span></span>
                        </div>
                    </div>
                    
                    <!-- Results Tables -->
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px;">
                        <div class="results-table-container">
                            <table class="results-table">
                                <thead><tr><th>Mode</th><th class="num">Passed</th><th class="num">Failed</th><th class="num">Total</th><th class="num">Rate</th></tr></thead>
                                <tbody id="pn-modes-body"></tbody>
                            </table>
                        </div>
                        <div class="results-table-container">
                            <table class="results-table">
                                <thead><tr><th>Channel</th><th class="num">Passed</th><th class="num">Failed</th><th class="num">Total</th><th class="num">Rate</th></tr></thead>
                                <tbody id="pn-results-body"></tbody>
                            </table>
                        </div>
                    </div>
                    
                    <!-- Totals Row -->
                    <div class="results-table-container">
                        <table class="results-table">
                            <tbody><tr style="background: #1a1a2a; font-weight: bold;"><td>TOTAL</td><td class="num pass" id="pn-total-passed">—</td><td class="num fail" id="pn-total-failed">—</td><td class="num" id="pn-total-tests">—</td><td class="num rate" id="pn-total-rate">—</td><td class="num" id="pn-total-ber">—</td></tr></tbody>
                        </table>
                    </div>
                    
                    <!-- Output Log -->
                    <div class="output-container">
                        <div class="output-header">
                            <span class="output-title">Live Output</span>
                            <div><button class="output-btn" onclick="pnClearOutput()">Clear</button> <button class="output-btn" onclick="pnCopyOutput()">Copy</button></div>
                        </div>
                        <div class="output-content" id="pn-output">
<span class="output-line header">==============================================</span>
<span class="output-line header">PhoenixNest M110A Test Suite</span>
<span class="output-line header">==============================================</span>
<span class="output-line">Ready to run tests.</span>
<span class="output-line">Configure options and click Start.</span>
                        </div>
                    </div>
                    
                    <!-- Export Bar -->
                    <div class="export-bar">
                        <span class="export-label">Export results:</span>
                        <div class="export-buttons">
                            <button class="btn btn-secondary" onclick="pnExportReport()">📄 Markdown</button>
                            <button class="btn btn-secondary" onclick="pnExportCSV()">📊 CSV</button>
                            <button class="btn btn-secondary" onclick="pnExportJSON()">{ } JSON</button>
                        </div>
                    </div>
                </div>
            </div>
        </div>
)HTML";

// ============================================================
// PHOENIX TESTS TAB JAVASCRIPT
// ============================================================
const char* HTML_JS_PHOENIX = R"JS(
        // Phoenix-specific state
        let pnTestRunning = false;
        let pnTestStartTime = null;
        let pnTestDurationSec = 0;
        let pnChannelResults = {};
        let pnModeResults = {};
        
        // Toggle multi-eq list visibility
        document.getElementById('pn-multi-eq').addEventListener('change', function() {
            document.getElementById('pn-eq-list').style.display = this.checked ? 'block' : 'none';
        });
        
        // Toggle server options visibility
        document.getElementById('pn-use-server').addEventListener('change', function() {
            document.getElementById('pn-server-opts').style.display = this.checked ? 'block' : 'none';
        });
        
        // Mode selection
        function pnSelectModes(which) {
            document.querySelectorAll('[id^="pn-mode-"]').forEach(cb => {
                if (which === 'all') cb.checked = true;
                else if (which === 'none') cb.checked = false;
                else if (which === 'short') cb.checked = cb.id.endsWith('s');
                else if (which === 'long') cb.checked = cb.id.endsWith('l');
                else if (which === 'fast') cb.checked = ['pn-mode-600s','pn-mode-600l','pn-mode-1200s','pn-mode-1200l','pn-mode-2400s','pn-mode-2400l'].includes(cb.id);
            });
        }
        
        // Presets
        function pnLoadPreset() {
            const p = document.getElementById('pn-test-preset').value;
            const presets = { quick: 1, standard: 3, extended: 10, overnight: 60 };
            document.getElementById('pn-duration').value = presets[p] || 3;
            if (p === 'quick') pnSelectModes('fast');
            else pnSelectModes('all');
        }
        
        function pnGetSelectedModes() {
            const modes = [];
            document.querySelectorAll('[id^="pn-mode-"]:checked').forEach(cb => {
                modes.push(cb.id.replace('pn-mode-', '').toUpperCase());
            });
            return modes.length === 12 ? '' : modes.join(',');
        }
        
        function pnGetSelectedEqs() {
            if (!document.getElementById('pn-multi-eq').checked) {
                return document.getElementById('pn-equalizer').value;
            }
            const eqs = [];
            if (document.getElementById('pn-eq-none').checked) eqs.push('NONE');
            if (document.getElementById('pn-eq-dfe').checked) eqs.push('DFE');
            if (document.getElementById('pn-eq-dfe-rls').checked) eqs.push('DFE_RLS');
            if (document.getElementById('pn-eq-mlse-l2').checked) eqs.push('MLSE_L2');
            if (document.getElementById('pn-eq-mlse-l3').checked) eqs.push('MLSE_L3');
            if (document.getElementById('pn-eq-turbo').checked) eqs.push('TURBO');
            return eqs.join(',');
        }
        
        function pnGetDurationSeconds() {
            const value = parseInt(document.getElementById('pn-duration').value) || 3;
            const unit = document.getElementById('pn-duration-unit').value;
            if (unit === 'sec') return value;
            if (unit === 'hr') return value * 3600;
            return value * 60;
        }
        
        // Build command line args
        function pnBuildArgs() {
            const args = [];
            
            // Duration or iterations
            const iterations = parseInt(document.getElementById('pn-iterations').value) || 0;
            if (iterations > 0) {
                args.push('--iterations', iterations.toString());
            } else {
                args.push('--duration', pnGetDurationSeconds().toString());
            }
            
            // Modes
            const modes = pnGetSelectedModes();
            if (modes) args.push('--modes', modes);
            
            // Equalizer
            const eqs = pnGetSelectedEqs();
            if (eqs.includes(',')) {
                args.push('--eqs', eqs);
            } else if (eqs !== 'DFE') {
                args.push('--eq', eqs);
            }
            
            // AFC algorithm
            const afc = document.getElementById('pn-afc').value;
            if (afc !== 'LEGACY') {
                args.push('--afc', afc);
            }
            
            // Progressive tests
            const progSnr = document.getElementById('pn-prog-snr').checked;
            const progFreq = document.getElementById('pn-prog-freq').checked;
            const progMp = document.getElementById('pn-prog-multipath').checked;
            if (progSnr && progFreq && progMp) {
                args.push('--progressive');
            } else {
                if (progSnr) args.push('--prog-snr');
                if (progFreq) args.push('--prog-freq');
                if (progMp) args.push('--prog-multipath');
            }
            
            // Server backend
            if (document.getElementById('pn-use-server').checked) {
                args.push('--server');
                const host = document.getElementById('pn-host').value;
                if (host && host !== '127.0.0.1') args.push('--host', host);
                const port = parseInt(document.getElementById('pn-port').value) || 4999;
                if (port !== 4999) args.push('--port', port.toString());
            }
            
            // Parallel
            const parallel = parseInt(document.getElementById('pn-parallel').value) || 1;
            if (parallel > 1) args.push('--parallel', parallel.toString());
            
            // Auto-detect mode
            if (document.getElementById('pn-auto-detect').checked) {
                args.push('--use-auto-detect');
            }
            
            // Reference sample test
            if (document.getElementById('pn-reference').checked) {
                args.push('--reference');
            }
            
            // CSV output
            if (document.getElementById('pn-opt-csv').checked) {
                args.push('--csv', 'reports/phoenix_results.csv');
            }
            
            return args;
        }
        
        // Start test
        async function pnStartTest() {
            if (pnTestRunning) return;
            pnTestRunning = true;
            pnTestStartTime = Date.now();
            pnTestDurationSec = pnGetDurationSeconds();
            
            document.getElementById('pn-btn-start').style.display = 'none';
            document.getElementById('pn-btn-stop').style.display = 'inline-block';
            document.getElementById('pn-status-dot').className = 'status-dot running';
            document.getElementById('pn-status-text').textContent = 'Running...';
            
            // Reset metrics
            document.getElementById('pn-metric-tests').textContent = '0';
            document.getElementById('pn-metric-passed').textContent = '0';
            document.getElementById('pn-metric-rate').textContent = '—';
            document.getElementById('pn-metric-ber').textContent = '—';
            document.getElementById('pn-progress-fill').style.width = '0%';
            document.getElementById('pn-progress-pct').textContent = '0';
            document.getElementById('pn-test-rating').textContent = '—';
            
            pnClearOutput();
            pnChannelResults = {};
            pnModeResults = {};
            document.getElementById('pn-results-body').innerHTML = '';
            document.getElementById('pn-modes-body').innerHTML = '';
            
            pnAddOutput('==============================================', 'header');
            pnAddOutput('PhoenixNest Exhaustive Test', 'header');
            pnAddOutput('==============================================', 'header');
            
            const args = pnBuildArgs();
            pnAddOutput('Command: exhaustive_test.exe ' + args.join(' '), 'info');
            pnAddOutput('Starting tests...', 'info');
            
            const params = new URLSearchParams();
            params.set('backend', 'phoenixnest');
            params.set('args', JSON.stringify(args));
            
            try {
                const response = await fetch('/run-exhaustive?' + params.toString());
                const reader = response.body.getReader();
                const decoder = new TextDecoder();
                
                while (pnTestRunning) {
                    const { value, done } = await reader.read();
                    if (done) break;
                    
                    const text = decoder.decode(value);
                    for (const line of text.split('\n')) {
                        if (line.startsWith('data: ')) {
                            try {
                                const data = JSON.parse(line.substring(6));
                                pnHandleTestEvent(data);
                            } catch (e) {}
                        }
                    }
                }
            } catch (err) {
                pnAddOutput('Error: ' + err.message, 'error');
            }
            
            pnFinishTest();
        }
        
        // Track progressive test results
        let pnProgressiveResults = {};
        
        function pnHandleTestEvent(data) {
            // Handle progressive test types
            if (data.type === 'progressive_start') {
                pnAddOutput('Testing ' + data.mode + '...', 'header');
                document.getElementById('pn-current-test').textContent = data.mode + ' (progressive)';
                return;
            }
            
            if (data.type === 'progress_step') {
                // Step in progressive test (snr/freq/multipath probing)
                const passClass = data.passed ? 'pass' : 'fail';
                pnAddOutput(data.output, passClass);
                return;
            }
            
            if (data.type === 'progressive_result') {
                // Final result for one test type
                pnAddOutput(data.output, 'pass');
                
                // Store progressive result
                if (!pnProgressiveResults[data.mode]) {
                    pnProgressiveResults[data.mode] = {};
                }
                pnProgressiveResults[data.mode][data.test] = data.value;
                
                // Update mode table with progressive results
                if (!pnModeResults[data.mode]) {
                    pnModeResults[data.mode] = { passed: 0, failed: 0, total: 0, rate: 0, progressive: {} };
                }
                pnModeResults[data.mode].progressive = pnModeResults[data.mode].progressive || {};
                pnModeResults[data.mode].progressive[data.test] = data.value;
                pnModeResults[data.mode].total = 1;
                pnModeResults[data.mode].passed = 1;
                pnModeResults[data.mode].rate = 100;
                pnRebuildModeTable();
                return;
            }
            
            if (data.type === 'summary') {
                pnAddOutput(data.output, 'header');
                return;
            }
            
            if (data.output) {
                let type = data.type || 'info';
                if (data.output.includes('PASS')) type = 'pass';
                else if (data.output.includes('FAIL')) type = 'fail';
                else if (data.output.includes('ERROR')) type = 'error';
                else if (data.output.includes('===')) type = 'header';
                pnAddOutput(data.output, type);
            }
            
            if (data.tests !== undefined) {
                document.getElementById('pn-metric-tests').textContent = data.tests;
                document.getElementById('pn-total-tests').textContent = data.tests;
            }
            if (data.passed !== undefined) {
                document.getElementById('pn-metric-passed').textContent = data.passed;
                document.getElementById('pn-total-passed').textContent = data.passed;
            }
            if (data.failed !== undefined) {
                document.getElementById('pn-total-failed').textContent = data.failed;
            }
            if (data.rate !== undefined) {
                const rateStr = data.rate.toFixed(1) + '%';
                document.getElementById('pn-metric-rate').textContent = rateStr;
                document.getElementById('pn-total-rate').textContent = rateStr;
            }
            if (data.ber !== undefined || data.avgBer !== undefined) {
                const ber = parseFloat(data.avgBer || data.ber);
                if (!isNaN(ber)) {
                    const berStr = ber < 0.0001 ? ber.toExponential(2) : ber.toFixed(6);
                    document.getElementById('pn-metric-ber').textContent = berStr;
                    document.getElementById('pn-total-ber').textContent = berStr;
                }
            }
            
            if (data.currentTest) {
                document.getElementById('pn-current-test').textContent = data.currentTest;
            }
            
            // Update progress
            if (data.progress !== undefined) {
                document.getElementById('pn-progress-fill').style.width = data.progress + '%';
                document.getElementById('pn-progress-pct').textContent = Math.round(data.progress);
            } else if (pnTestStartTime && pnTestDurationSec > 0) {
                const elapsed = (Date.now() - pnTestStartTime) / 1000;
                const progress = Math.min(100, (elapsed / pnTestDurationSec) * 100);
                document.getElementById('pn-progress-fill').style.width = progress + '%';
                document.getElementById('pn-progress-pct').textContent = Math.round(progress);
            }
            
            // Update elapsed/remaining time
            if (pnTestStartTime) {
                const elapsed = Math.floor((Date.now() - pnTestStartTime) / 1000);
                const mins = Math.floor(elapsed / 60);
                const secs = elapsed % 60;
                document.getElementById('pn-elapsed-time').textContent = mins + ':' + secs.toString().padStart(2, '0');
                
                if (pnTestDurationSec > 0) {
                    const remaining = Math.max(0, pnTestDurationSec - elapsed);
                    const rmins = Math.floor(remaining / 60);
                    const rsecs = Math.floor(remaining % 60);
                    document.getElementById('pn-remaining-time').textContent = rmins + ':' + rsecs.toString().padStart(2, '0');
                }
            }
            
            if (data.rating) {
                const ratingEl = document.getElementById('pn-test-rating');
                ratingEl.textContent = data.rating;
                if (data.rating === 'EXCELLENT') ratingEl.style.color = '#00ff88';
                else if (data.rating === 'GOOD') ratingEl.style.color = '#00d4ff';
                else if (data.rating === 'FAIR') ratingEl.style.color = '#ffaa00';
                else ratingEl.style.color = '#ff3a50';
            }
            
            if (data.type === 'mode_stat' && data.mode) {
                pnModeResults[data.mode] = { passed: data.passed, failed: data.failed, total: data.total, rate: data.rate };
                pnRebuildModeTable();
            }
            
            if (data.type === 'channel_stat' && data.channel) {
                pnChannelResults[data.channel] = { passed: data.passed, failed: data.failed, total: data.total, rate: data.rate, avgBer: data.avgBer };
                pnRebuildChannelTable();
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
                    if (!pnModeResults[mode]) {
                        pnModeResults[mode] = { passed: 0, failed: 0, total: 0, rate: 0 };
                    }
                    pnModeResults[mode].total++;
                    if (passed) pnModeResults[mode].passed++;
                    else pnModeResults[mode].failed++;
                    pnModeResults[mode].rate = (pnModeResults[mode].passed / pnModeResults[mode].total) * 100;
                    pnRebuildModeTable();
                    
                    // Accumulate channel stats
                    if (!pnChannelResults[channel]) {
                        pnChannelResults[channel] = { passed: 0, failed: 0, total: 0, rate: 0, totalBer: 0, avgBer: 0 };
                    }
                    pnChannelResults[channel].total++;
                    if (passed) pnChannelResults[channel].passed++;
                    else pnChannelResults[channel].failed++;
                    pnChannelResults[channel].totalBer = (pnChannelResults[channel].totalBer || 0) + ber;
                    pnChannelResults[channel].avgBer = pnChannelResults[channel].totalBer / pnChannelResults[channel].total;
                    pnChannelResults[channel].rate = (pnChannelResults[channel].passed / pnChannelResults[channel].total) * 100;
                    pnRebuildChannelTable();
                }
            }
            
            if (data.done) {
                pnTestRunning = false;
            }
        }
        
        function pnRebuildModeTable() {
            const tbody = document.getElementById('pn-modes-body');
            tbody.innerHTML = '';
            const sortOrder = ['75S', '75L', '150S', '150L', '300S', '300L', '600S', '600L', '1200S', '1200L', '2400S', '2400L'];
            const modes = Object.keys(pnModeResults).sort((a, b) => {
                const ia = sortOrder.indexOf(a);
                const ib = sortOrder.indexOf(b);
                if (ia === -1 && ib === -1) return a.localeCompare(b);
                if (ia === -1) return 1;
                if (ib === -1) return -1;
                return ia - ib;
            });
            for (const m of modes) {
                const r = pnModeResults[m];
                const rateClass = r.rate >= 80 ? 'pass' : r.rate >= 50 ? 'rate' : 'fail';
                const row = document.createElement('tr');
                row.innerHTML = '<td>' + m + '</td><td class="num pass">' + r.passed + '</td><td class="num fail">' + r.failed + '</td><td class="num">' + r.total + '</td><td class="num ' + rateClass + '">' + r.rate.toFixed(1) + '%</td>';
                tbody.appendChild(row);
            }
        }
        
        function pnRebuildChannelTable() {
            const tbody = document.getElementById('pn-results-body');
            tbody.innerHTML = '';
            const channels = Object.keys(pnChannelResults);
            for (const ch of channels) {
                const r = pnChannelResults[ch];
                const rateClass = r.rate >= 80 ? 'pass' : r.rate >= 50 ? 'rate' : 'fail';
                const row = document.createElement('tr');
                row.innerHTML = '<td>' + ch + '</td><td class="num pass">' + r.passed + '</td><td class="num fail">' + r.failed + '</td><td class="num">' + r.total + '</td><td class="num ' + rateClass + '">' + r.rate.toFixed(1) + '%</td>';
                tbody.appendChild(row);
            }
        }
        
        function pnFinishTest() {
            pnTestRunning = false;
            document.getElementById('pn-btn-start').style.display = 'inline-block';
            document.getElementById('pn-btn-stop').style.display = 'none';
            
            const passRate = parseFloat(document.getElementById('pn-metric-rate').textContent) || 0;
            if (passRate >= 95) {
                document.getElementById('pn-status-dot').className = 'status-dot pass';
                document.getElementById('pn-status-text').textContent = 'Complete - EXCELLENT';
            } else if (passRate >= 80) {
                document.getElementById('pn-status-dot').className = 'status-dot pass';
                document.getElementById('pn-status-text').textContent = 'Complete - GOOD';
            } else {
                document.getElementById('pn-status-dot').className = 'status-dot fail';
                document.getElementById('pn-status-text').textContent = 'Complete - Needs Work';
            }
            
            document.getElementById('pn-progress-fill').style.width = '100%';
            document.getElementById('pn-progress-pct').textContent = '100';
            document.getElementById('pn-current-test').textContent = 'Done';
        }
        
        function pnStopTest() {
            pnTestRunning = false;
            fetch('/stop-test');
            pnAddOutput('Test stopped by user', 'warning');
            pnFinishTest();
        }
        
        function pnResetConfig() {
            document.getElementById('pn-test-preset').value = 'standard';
            pnLoadPreset();
            document.getElementById('pn-iterations').value = '0';
            document.getElementById('pn-equalizer').value = 'DFE';
            document.getElementById('pn-multi-eq').checked = false;
            document.getElementById('pn-eq-list').style.display = 'none';
            document.getElementById('pn-afc').value = 'LEGACY';
            document.getElementById('pn-prog-snr').checked = false;
            document.getElementById('pn-prog-freq').checked = false;
            document.getElementById('pn-prog-multipath').checked = false;
            document.getElementById('pn-use-server').checked = false;
            document.getElementById('pn-server-opts').style.display = 'none';
            document.getElementById('pn-auto-detect').checked = false;
            document.getElementById('pn-reference').checked = false;
            document.getElementById('pn-parallel').value = '1';
            document.getElementById('pn-opt-csv').checked = false;
        }
        
        function pnAddOutput(text, type) {
            const output = document.getElementById('pn-output');
            const line = document.createElement('span');
            line.className = 'output-line ' + (type || '');
            line.textContent = text;
            output.appendChild(line);
            output.scrollTop = output.scrollHeight;
        }
        
        function pnClearOutput() {
            document.getElementById('pn-output').innerHTML = '';
        }
        
        function pnCopyOutput() {
            const text = document.getElementById('pn-output').innerText;
            navigator.clipboard.writeText(text);
        }
        
        function pnExportReport() {
            window.open('/export-report?backend=phoenixnest&format=md', '_blank');
        }
        
        function pnExportCSV() {
            window.open('/export-report?backend=phoenixnest&format=csv', '_blank');
        }
        
        function pnExportJSON() {
            window.open('/export-report?backend=phoenixnest&format=json', '_blank');
        }
)JS";

} // namespace test_gui
