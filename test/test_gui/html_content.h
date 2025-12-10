#pragma once
/**
 * @file html_content.h
 * @brief Embedded HTML/CSS/JS for M110A Test GUI
 * 
 * Contains the complete web interface with:
 * - Run Tests tab with comprehensive configuration
 * - Cross-Modem Interop tab
 * - Reports tab
 */

namespace test_gui {

const char* HTML_PAGE = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <title>M110A Modem Test Suite</title>
    <style>
        * { box-sizing: border-box; }
        body { font-family: 'Consolas', 'Monaco', monospace; margin: 0; padding: 20px; background: #0a0a12; color: #c8d0e0; line-height: 1.4; }
        h1 { color: #00d4ff; margin-bottom: 20px; font-size: 24px; }
        h2 { color: #00d4ff; font-size: 14px; text-transform: uppercase; letter-spacing: 2px; margin: 0 0 12px 0; }
        h3 { color: #8892a8; font-size: 11px; text-transform: uppercase; letter-spacing: 1px; margin: 12px 0 8px 0; }
        .container { max-width: 1500px; margin: 0 auto; }
        
        /* Tabs */
        .tabs { display: flex; gap: 5px; margin-bottom: 0; }
        .tab { padding: 12px 25px; background: #12121e; border: 1px solid #252538; border-bottom: none; border-radius: 8px 8px 0 0; color: #888; cursor: pointer; font-weight: bold; font-family: inherit; }
        .tab.active { color: #00d4ff; border-bottom: 2px solid #00d4ff; }
        .tab:hover { color: #00d4ff; }
        .tab-content { display: none; background: #12121e; border: 1px solid #252538; border-radius: 0 8px 8px 8px; padding: 20px; }
        .tab-content.active { display: block; }
        
        /* Run Tests Layout */
        .test-layout { display: grid; grid-template-columns: 450px 1fr; gap: 20px; }
        @media (max-width: 1100px) { .test-layout { grid-template-columns: 1fr; } }
        
        /* Control Panel */
        .control-panel { background: #0f0f1a; border: 1px solid #1e1e2e; border-radius: 4px; padding: 16px; max-height: 85vh; overflow-y: auto; }
        .control-section { margin-bottom: 20px; padding-bottom: 16px; border-bottom: 1px solid #1e1e2e; }
        .control-section:last-child { border-bottom: none; margin-bottom: 0; }
        
        /* Form elements */
        .form-row { display: flex; gap: 12px; margin-bottom: 10px; flex-wrap: wrap; }
        .form-group { display: flex; flex-direction: column; flex: 1; min-width: 80px; }
        .form-group.narrow { flex: 0 0 90px; }
        label { font-size: 10px; text-transform: uppercase; letter-spacing: 1px; color: #6a7080; margin-bottom: 4px; }
        select, input[type="number"], input[type="text"] { background: #0a0a12; border: 1px solid #2a2a3a; border-radius: 3px; color: #c8d0e0; padding: 8px 10px; font-family: inherit; font-size: 13px; width: 100%; }
        select:focus, input:focus { outline: none; border-color: #00d4ff; }
        
        /* Radio group */
        .radio-group { display: flex; flex-direction: column; gap: 8px; }
        .radio-item { display: flex; align-items: flex-start; gap: 10px; padding: 10px 12px; background: #0a0a12; border: 1px solid #1e1e2e; border-radius: 3px; cursor: pointer; }
        .radio-item:hover { border-color: #3a3a4a; }
        .radio-item.selected { border-color: #00d4ff; background: rgba(0,212,255,0.08); }
        .radio-item input[type="radio"] { width: 16px; height: 16px; accent-color: #00d4ff; }
        .radio-content label { font-size: 12px; text-transform: none; letter-spacing: 0; color: #c8d0e0; cursor: pointer; margin: 0; }
        .radio-desc { font-size: 10px; color: #5a6070; }
        .radio-item.selected .radio-content label { color: #00d4ff; }
        
        /* Subsection */
        .subsection { margin-top: 12px; padding: 12px; background: #0a0a12; border: 1px solid #1a1a2a; border-radius: 3px; }
        .subsection h3 { margin-top: 0; }
        
        /* Connection status */
        .connection-status { display: flex; align-items: center; gap: 8px; margin-top: 10px; padding: 8px 12px; background: #0a0a12; border-radius: 3px; font-size: 11px; color: #6a7080; }
        .connection-status.connected { color: #00ff88; }
        .connection-status.error { color: #ff3a50; }
        
        /* Checkbox grid */
        .checkbox-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 6px; }
        .checkbox-grid.three-col { grid-template-columns: repeat(3, 1fr); }
        .checkbox-grid.four-col { grid-template-columns: repeat(4, 1fr); }
        .checkbox-item { display: flex; align-items: center; gap: 6px; padding: 6px 8px; background: #0a0a12; border: 1px solid #1e1e2e; border-radius: 3px; cursor: pointer; }
        .checkbox-item:hover { border-color: #3a3a4a; }
        .checkbox-item input[type="checkbox"] { width: 14px; height: 14px; accent-color: #00d4ff; }
        .checkbox-item label { font-size: 11px; color: #a0a8b8; cursor: pointer; margin: 0; }
        .checkbox-item:has(input:checked) { border-color: #00d4ff; background: rgba(0,212,255,0.08); }
        .checkbox-item:has(input:checked) label { color: #00d4ff; }
        
        /* Quick select */
        .quick-select { display: flex; gap: 6px; margin-bottom: 8px; flex-wrap: wrap; }
        .quick-btn { padding: 4px 10px; font-size: 10px; text-transform: uppercase; background: #1a1a2a; border: 1px solid #2a2a3a; border-radius: 3px; color: #8892a8; cursor: pointer; font-family: inherit; }
        .quick-btn:hover { background: #252538; color: #c8d0e0; }
        
        /* Hint text */
        .hint-text { margin-top: 8px; padding: 8px 10px; background: rgba(255,170,0,0.1); border-left: 2px solid #ffaa00; font-size: 11px; color: #aa8800; }
        
        /* Buttons */
        .action-bar { display: flex; gap: 10px; margin-top: 16px; }
        .btn { padding: 12px 24px; font-family: inherit; font-size: 12px; font-weight: bold; text-transform: uppercase; letter-spacing: 1px; border: none; border-radius: 3px; cursor: pointer; }
        .btn-primary { background: #00d4ff; color: #000; }
        .btn-primary:hover { background: #00b8dd; }
        .btn-primary:disabled { background: #2a2a3a; color: #4a4a5a; cursor: not-allowed; }
        .btn-danger { background: #ff3a50; color: #fff; }
        .btn-secondary { background: #2a2a3a; color: #8892a8; }
        .btn-secondary:hover { background: #3a3a4a; }
        
        /* Results Panel */
        .results-panel { display: flex; flex-direction: column; gap: 16px; }
        
        /* Status Bar */
        .status-bar { background: #0f0f1a; border: 1px solid #1e1e2e; border-radius: 4px; padding: 12px 16px; display: flex; align-items: center; gap: 20px; flex-wrap: wrap; }
        .status-indicator { display: flex; align-items: center; gap: 8px; }
        .status-dot { width: 10px; height: 10px; border-radius: 50%; background: #3a3a4a; }
        .status-dot.running { background: #00d4ff; animation: pulse 1s infinite; }
        .status-dot.pass { background: #00ff88; }
        .status-dot.fail { background: #ff3a50; }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }
        .status-metrics { display: flex; gap: 24px; margin-left: auto; }
        .metric { text-align: center; }
        .metric-value { font-size: 20px; font-weight: bold; color: #00d4ff; }
        .metric-label { font-size: 9px; text-transform: uppercase; color: #6a7080; }
        
        /* Progress */
        .progress-container { background: #0f0f1a; border: 1px solid #1e1e2e; border-radius: 4px; padding: 12px 16px; }
        .progress-header { display: flex; justify-content: space-between; margin-bottom: 8px; }
        .progress-label, .progress-value { font-size: 11px; color: #6a7080; }
        .progress-value { color: #00d4ff; }
        .progress-bar { height: 6px; background: #1a1a2a; border-radius: 3px; overflow: hidden; }
        .progress-fill { height: 100%; background: linear-gradient(90deg, #00d4ff, #00ff88); width: 0%; transition: width 0.3s; }
        .progress-details { display: flex; justify-content: space-between; margin-top: 8px; font-size: 10px; color: #4a4a5a; }
        
        /* Results table */
        .results-table-container { background: #0f0f1a; border: 1px solid #1e1e2e; border-radius: 4px; overflow: hidden; }
        .results-table { width: 100%; border-collapse: collapse; }
        .results-table th { background: #1a1a2a; padding: 10px 12px; text-align: left; font-size: 10px; text-transform: uppercase; color: #6a7080; border-bottom: 1px solid #252538; }
        .results-table th.num { text-align: right; }
        .results-table td { padding: 10px 12px; font-size: 12px; border-bottom: 1px solid #1e1e2e; }
        .results-table td.num { text-align: right; font-family: monospace; }
        .results-table .pass { color: #00ff88; }
        .results-table .fail { color: #ff3a50; }
        .results-table .rate { color: #00d4ff; }
        .results-table tfoot td { background: #1a1a2a; font-weight: bold; }
        
        /* Output log */
        .output-container { background: #0f0f1a; border: 1px solid #1e1e2e; border-radius: 4px; flex: 1; min-height: 200px; display: flex; flex-direction: column; }
        .output-header { display: flex; justify-content: space-between; padding: 10px 16px; border-bottom: 1px solid #1e1e2e; }
        .output-title { font-size: 11px; text-transform: uppercase; color: #6a7080; }
        .output-btn { padding: 4px 10px; font-size: 10px; background: #1a1a2a; border: 1px solid #2a2a3a; border-radius: 3px; color: #6a7080; cursor: pointer; font-family: inherit; }
        .output-content { flex: 1; padding: 12px 16px; overflow-y: auto; font-size: 12px; line-height: 1.6; color: #8892a8; max-height: 300px; }
        .output-line { margin-bottom: 2px; }
        .output-line.header { color: #00d4ff; font-weight: bold; }
        .output-line.pass { color: #00ff88; }
        .output-line.fail { color: #ff3a50; }
        
        /* Export bar */
        .export-bar { background: #0f0f1a; border: 1px solid #1e1e2e; border-radius: 4px; padding: 12px 16px; display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px; }
        .export-label { font-size: 11px; color: #6a7080; }
        .export-buttons { display: flex; gap: 8px; }
        
        /* Interop styles */
        .interop-section { background: #0f3460; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
        .interop-section h3 { color: #00d4ff; margin: 0 0 15px 0; }
        .interop-config { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 15px; margin-bottom: 15px; }
        .interop-status { display: flex; align-items: center; gap: 10px; padding: 10px 15px; background: #16213e; border-radius: 4px; }
        .btn-connect { background: #00d4ff; color: #000; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-weight: bold; font-family: inherit; }
        .btn-disconnect { background: #ff4757; color: #fff; }
        
        .sub-tabs { display: flex; gap: 10px; margin-bottom: 20px; flex-wrap: wrap; }
        .sub-tab { padding: 10px 20px; border: 1px solid #333; border-radius: 20px; background: #16213e; color: #888; cursor: pointer; font-size: 13px; font-family: inherit; }
        .sub-tab.active { background: #00d4ff; color: #000; border-color: #00d4ff; font-weight: bold; }
        .sub-tab-content { display: none; }
        .sub-tab-content.active { display: block; }
        
        .test-steps { list-style: none; padding: 0; margin: 15px 0; }
        .test-steps li { padding: 8px 0; display: flex; align-items: center; gap: 10px; border-bottom: 1px solid #333; font-size: 13px; }
        .step-icon { width: 20px; text-align: center; }
        .step-pending { color: #666; }
        .step-running { color: #ff9f43; }
        .step-complete { color: #5fff5f; }
        .step-error { color: #ff4757; }
        
        .matrix-table { width: 100%; border-collapse: collapse; }
        .matrix-table th, .matrix-table td { padding: 10px; text-align: center; border: 1px solid #333; }
        .matrix-table th { background: #0f3460; color: #00d4ff; }
        .matrix-cell { font-size: 16px; }
        .matrix-pass { color: #5fff5f; }
        .matrix-fail { color: #ff4757; }
        .matrix-pending { color: #666; }
        .matrix-running { color: #ff9f43; animation: pulse 1s infinite; }
        
        .interop-log { background: #0f0f23; border: 1px solid #333; border-radius: 4px; padding: 10px; height: 200px; overflow-y: auto; font-size: 12px; margin-top: 15px; }
        .log-tx { color: #ff9f43; }
        .log-rx { color: #5fff5f; }
        .log-error { color: #ff4757; }
        
        /* Reports */
        .report-item { background: #0f3460; padding: 15px; border-radius: 8px; margin-bottom: 10px; display: flex; justify-content: space-between; align-items: center; }
        .report-name { color: #00d4ff; font-weight: bold; }
        .report-date { color: #888; font-size: 12px; }
        .report-link { color: #00d4ff; text-decoration: none; }
    </style>
</head>
<body>
    <div class="container">
        <h1>M110A Modem Test Suite</h1>
        
        <div class="tabs">
            <button class="tab active" onclick="showTab('tests')">Run Tests</button>
            <button class="tab" onclick="showTab('interop')">Cross-Modem Interop</button>
            <button class="tab" onclick="showTab('reports')">Reports</button>
        </div>
        
        <!-- ============ RUN TESTS TAB ============ -->
        <div id="tab-tests" class="tab-content active">
            <div class="test-layout">
                <!-- LEFT: Control Panel -->
                <div class="control-panel">
                    
                    <!-- Backend Selection -->
                    <div class="control-section">
                        <h2>Test Backend</h2>
                        <div class="radio-group">
                            <div class="radio-item selected" onclick="selectBackend('direct')">
                                <input type="radio" name="backend" id="backend-direct" checked>
                                <div class="radio-content">
                                    <label for="backend-direct">Direct API</label>
                                    <span class="radio-desc">Call encode/decode directly (fastest)</span>
                                </div>
                            </div>
                            <div class="radio-item" onclick="selectBackend('tcp-local')">
                                <input type="radio" name="backend" id="backend-tcp-local">
                                <div class="radio-content">
                                    <label for="backend-tcp-local">TCP Server (Local)</label>
                                    <span class="radio-desc">Connect to m110a_server.exe on localhost</span>
                                </div>
                            </div>
                            <div class="radio-item" onclick="selectBackend('tcp-remote')">
                                <input type="radio" name="backend" id="backend-tcp-remote">
                                <div class="radio-content">
                                    <label for="backend-tcp-remote">TCP Server (Remote)</label>
                                    <span class="radio-desc">Connect to remote server instance</span>
                                </div>
                            </div>
                        </div>
                        <div id="tcp-settings" class="subsection" style="display: none;">
                            <h3>Server Connection</h3>
                            <div class="form-row">
                                <div class="form-group"><label>Host</label><input type="text" id="tcp-host" value="127.0.0.1"></div>
                                <div class="form-group narrow"><label>Ctrl Port</label><input type="number" id="tcp-ctrl-port" value="5100"></div>
                                <div class="form-group narrow"><label>Data Port</label><input type="number" id="tcp-data-port" value="5101"></div>
                            </div>
                            <div class="form-row">
                                <div class="form-group narrow"><label>Timeout (ms)</label><input type="number" id="tcp-timeout" value="5000"></div>
                                <div class="form-group"><label>&nbsp;</label><button class="quick-btn" onclick="testConnection()">Test Connection</button></div>
                            </div>
                            <div class="connection-status" id="tcp-conn-status"><span class="status-dot"></span><span>Not connected</span></div>
                        </div>
                    </div>
                    
                    <!-- Parallelization -->
                    <div class="control-section">
                        <h2>Parallelization</h2>
                        <div class="form-row">
                            <div class="form-group narrow">
                                <label>Workers</label>
                                <select id="num-workers">
                                    <option value="1" selected>1 (Sequential)</option>
                                    <option value="2">2 Threads</option>
                                    <option value="4">4 Threads</option>
                                    <option value="8">8 Threads</option>
                                </select>
                            </div>
                            <div class="form-group narrow"><label>Batch Size</label><input type="number" id="batch-size" value="10" min="1" max="100"></div>
                            <div class="form-group">
                                <label>Mode</label>
                                <select id="parallel-mode">
                                    <option value="by-mode">Parallel by Mode</option>
                                    <option value="by-category">Parallel by Category</option>
                                    <option value="by-iteration">Parallel by Iteration</option>
                                </select>
                            </div>
                        </div>
                        <div class="hint-text">⚠️ TCP backend limited to 1 worker per server instance</div>
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
                            <div class="checkbox-item"><input type="checkbox" id="mode-4800s" checked><label for="mode-4800s">4800S</label></div>
                        </div>
                    </div>
                    
                    <!-- Test Categories -->
                    <div class="control-section">
                        <h2>Test Categories</h2>
                        <div class="checkbox-grid">
                            <div class="checkbox-item"><input type="checkbox" id="cat-clean" checked><label for="cat-clean">Clean Loopback</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-awgn" checked><label for="cat-awgn">AWGN Channel</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-multipath" checked><label for="cat-multipath">Multipath</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-freqoff" checked><label for="cat-freqoff">Freq Offset</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-sizes" checked><label for="cat-sizes">Message Sizes</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-random" checked><label for="cat-random">Random Data</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-dfe"><label for="cat-dfe">DFE Equalizer</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="cat-mlse"><label for="cat-mlse">MLSE Equalizer</label></div>
                        </div>
                    </div>
                    
                    <!-- Channel Parameters -->
                    <div class="control-section">
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
                        <div class="form-row" style="margin-top: 8px;">
                            <div class="form-group narrow"><label>Echo Gain</label><input type="number" id="echo-gain" value="0.5" min="0" max="1" step="0.1"></div>
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
                    
                    <!-- Message Options -->
                    <div class="control-section">
                        <h2>Message Options</h2>
                        <div class="form-row">
                            <div class="form-group"><label>Test Message</label><input type="text" id="test-message" value="THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG"></div>
                        </div>
                        <h3>Message Sizes (bytes)</h3>
                        <div class="checkbox-grid four-col">
                            <div class="checkbox-item"><input type="checkbox" id="size-10" checked><label for="size-10">10</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="size-50" checked><label for="size-50">50</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="size-100" checked><label for="size-100">100</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="size-200" checked><label for="size-200">200</label></div>
                            <div class="checkbox-item"><input type="checkbox" id="size-500"><label for="size-500">500</label></div>
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
                            <span>Iteration: <span id="iteration">0</span></span>
                        </div>
                    </div>
                    
                    <!-- Results Table -->
                    <div class="results-table-container">
                        <table class="results-table">
                            <thead><tr><th>Category</th><th class="num">Passed</th><th class="num">Failed</th><th class="num">Total</th><th class="num">Rate</th><th class="num">Avg BER</th></tr></thead>
                            <tbody id="results-body">
                                <tr><td>Clean Loopback</td><td class="num pass">—</td><td class="num fail">—</td><td class="num">—</td><td class="num rate">—</td><td class="num">—</td></tr>
                                <tr><td>AWGN Channel</td><td class="num pass">—</td><td class="num fail">—</td><td class="num">—</td><td class="num rate">—</td><td class="num">—</td></tr>
                                <tr><td>Multipath</td><td class="num pass">—</td><td class="num fail">—</td><td class="num">—</td><td class="num rate">—</td><td class="num">—</td></tr>
                                <tr><td>Freq Offset</td><td class="num pass">—</td><td class="num fail">—</td><td class="num">—</td><td class="num rate">—</td><td class="num">—</td></tr>
                                <tr><td>Message Sizes</td><td class="num pass">—</td><td class="num fail">—</td><td class="num">—</td><td class="num rate">—</td><td class="num">—</td></tr>
                                <tr><td>Random Data</td><td class="num pass">—</td><td class="num fail">—</td><td class="num">—</td><td class="num rate">—</td><td class="num">—</td></tr>
                                <tr><td>DFE Equalizer</td><td class="num pass">—</td><td class="num fail">—</td><td class="num">—</td><td class="num rate">—</td><td class="num">—</td></tr>
                                <tr><td>MLSE Equalizer</td><td class="num pass">—</td><td class="num fail">—</td><td class="num">—</td><td class="num rate">—</td><td class="num">—</td></tr>
                            </tbody>
                            <tfoot><tr><td>TOTAL</td><td class="num pass" id="total-passed">—</td><td class="num fail" id="total-failed">—</td><td class="num" id="total-tests">—</td><td class="num rate" id="total-rate">—</td><td class="num" id="total-ber">—</td></tr></tfoot>
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
<span class="output-line header">M110A Exhaustive Test Suite</span>
<span class="output-line header">==============================================</span>
<span class="output-line">Ready to run tests.</span>
<span class="output-line">Configure options on the left and click Start.</span>
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
        
        <!-- ============ INTEROP TAB ============ -->
        <div id="tab-interop" class="tab-content">
            <div class="sub-tabs">
                <button class="sub-tab active" onclick="showSubTab('setup')">🔧 Connection Setup</button>
                <button class="sub-tab" onclick="showSubTab('brain-pn')">🧠 Brain → PhoenixNest</button>
                <button class="sub-tab" onclick="showSubTab('pn-brain')">🚀 PhoenixNest → Brain</button>
                <button class="sub-tab" onclick="showSubTab('matrix')">📊 Full Matrix</button>
            </div>
            
            <div id="subtab-setup" class="sub-tab-content active">
                <div class="interop-section">
                    <h3>🚀 PhoenixNest Server</h3>
                    <div class="interop-config">
                        <div class="form-group"><label>Control Port</label><input type="number" id="pn-ctrl-port" value="5100"></div>
                        <div class="form-group"><label>Data Port</label><input type="number" id="pn-data-port" value="5101"></div>
                    </div>
                    <div class="interop-status">
                        <span class="status-dot" id="pn-status-dot"></span>
                        <span id="pn-status-text">Server Stopped</span>
                        <button class="btn-connect" id="btn-pn-server" onclick="togglePnServer()">Start Server</button>
                    </div>
                </div>
                <div class="interop-section">
                    <h3>🧠 Paul Brain Modem</h3>
                    <div class="interop-config">
                        <div class="form-group"><label>Host</label><input type="text" id="brain-host" value="localhost"></div>
                        <div class="form-group"><label>Control Port</label><input type="number" id="brain-ctrl-port" value="3999"></div>
                        <div class="form-group"><label>Data Port</label><input type="number" id="brain-data-port" value="3998"></div>
                    </div>
                    <div class="interop-status">
                        <span class="status-dot" id="brain-status-dot"></span>
                        <span id="brain-status-text">Disconnected</span>
                        <button class="btn-connect" id="btn-brain-connect" onclick="toggleBrainConnection()">Connect</button>
                    </div>
                </div>
            </div>
            
            <div id="subtab-brain-pn" class="sub-tab-content">
                <h3>🧠📤 Brain TX → 🚀📥 PhoenixNest RX</h3>
                <div class="form-row">
                    <div class="form-group"><label>Mode</label><select id="brain-pn-mode"><option value="600S" selected>600 bps Short</option></select></div>
                    <div class="form-group"><label>Message</label><input type="text" id="brain-pn-msg" value="HELLO CROSS MODEM TEST"></div>
                    <button class="btn btn-primary" onclick="runBrainToPnTest()">▶ Run Test</button>
                </div>
                <ul class="test-steps" id="brain-pn-steps">
                    <li><span class="step-icon step-pending">○</span> Set Brain data rate</li>
                    <li><span class="step-icon step-pending">○</span> Send test message</li>
                    <li><span class="step-icon step-pending">○</span> Wait for TX complete</li>
                    <li><span class="step-icon step-pending">○</span> Inject PCM to PhoenixNest</li>
                    <li><span class="step-icon step-pending">○</span> Compare decoded data</li>
                </ul>
                <div id="brain-pn-result" style="padding:10px; background:#333; border-radius:4px;">Result will appear here</div>
            </div>
            
            <div id="subtab-pn-brain" class="sub-tab-content">
                <h3>🚀📤 PhoenixNest TX → 🧠📥 Brain RX</h3>
                <div class="form-row">
                    <div class="form-group"><label>Mode</label><select id="pn-brain-mode"><option value="600S" selected>600 bps Short</option></select></div>
                    <div class="form-group"><label>Message</label><input type="text" id="pn-brain-msg" value="HELLO CROSS MODEM TEST"></div>
                    <button class="btn btn-primary" onclick="runPnToBrainTest()">▶ Run Test</button>
                </div>
                <ul class="test-steps" id="pn-brain-steps">
                    <li><span class="step-icon step-pending">○</span> Set PhoenixNest data rate</li>
                    <li><span class="step-icon step-pending">○</span> Send test message</li>
                    <li><span class="step-icon step-pending">○</span> Wait for TX complete</li>
                    <li><span class="step-icon step-pending">○</span> Inject PCM to Brain</li>
                    <li><span class="step-icon step-pending">○</span> Compare decoded data</li>
                </ul>
                <div id="pn-brain-result" style="padding:10px; background:#333; border-radius:4px;">Result will appear here</div>
            </div>
            
            <div id="subtab-matrix" class="sub-tab-content">
                <h3>📊 Cross-Modem Compatibility Matrix</h3>
                <button class="btn btn-primary" onclick="runMatrix()">▶ Run All Tests</button>
                <span id="matrix-progress" style="margin-left:20px; color:#888;">0/24</span>
                <table class="matrix-table" style="margin-top:15px;">
                    <thead><tr><th>Mode</th><th>Brain → PN</th><th>PN → Brain</th></tr></thead>
                    <tbody id="matrix-body"></tbody>
                </table>
            </div>
            
            <div class="interop-log" id="interop-log"><div class="log-info">[INFO] Interop Test Log</div></div>
        </div>
        
        <!-- ============ REPORTS TAB ============ -->
        <div id="tab-reports" class="tab-content">
            <h2>Test Reports</h2>
            <button class="btn btn-secondary" onclick="loadReports()">🔄 Refresh</button>
            <div id="reports-list" style="margin-top:20px;">Loading...</div>
        </div>
    </div>
    
    <script>
        let testRunning = false;
        let currentBackend = 'direct';
        let pnServerRunning = false;
        let brainConnected = false;
        
        // Tab navigation
        function showTab(name) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            document.querySelector(`.tab[onclick*="${name}"]`).classList.add('active');
            document.getElementById('tab-' + name).classList.add('active');
            if (name === 'reports') loadReports();
        }
        
        function showSubTab(name) {
            document.querySelectorAll('.sub-tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.sub-tab-content').forEach(t => t.classList.remove('active'));
            document.querySelector(`.sub-tab[onclick*="${name}"]`).classList.add('active');
            document.getElementById('subtab-' + name).classList.add('active');
        }
        
        // Backend selection
        function selectBackend(backend) {
            currentBackend = backend;
            document.querySelectorAll('.radio-item').forEach(i => i.classList.remove('selected'));
            document.getElementById('backend-' + backend).checked = true;
            document.getElementById('backend-' + backend).closest('.radio-item').classList.add('selected');
            document.getElementById('tcp-settings').style.display = (backend !== 'direct') ? 'block' : 'none';
            if (backend === 'tcp-local') document.getElementById('tcp-host').value = '127.0.0.1';
        }
        
        function testConnection() {
            const host = document.getElementById('tcp-host').value;
            const port = document.getElementById('tcp-ctrl-port').value;
            const status = document.getElementById('tcp-conn-status');
            status.innerHTML = '<span class="status-dot running"></span><span>Connecting...</span>';
            fetch(`/test-connection?host=${encodeURIComponent(host)}&port=${port}`)
                .then(r => r.json())
                .then(d => {
                    if (d.success) {
                        status.className = 'connection-status connected';
                        status.innerHTML = '<span class="status-dot pass"></span><span>Connected: ' + (d.version || 'OK') + '</span>';
                    } else {
                        status.className = 'connection-status error';
                        status.innerHTML = '<span class="status-dot fail"></span><span>Failed: ' + (d.error || 'Unknown') + '</span>';
                    }
                }).catch(e => {
                    status.className = 'connection-status error';
                    status.innerHTML = '<span class="status-dot fail"></span><span>Error: ' + e.message + '</span>';
                });
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
        
        // Gather config
        function gatherConfig() {
            const getModes = () => {
                const modes = [];
                document.querySelectorAll('[id^="mode-"]:checked').forEach(cb => {
                    modes.push(cb.id.replace('mode-', '').toUpperCase());
                });
                return modes;
            };
            const getCheckedValues = (prefix) => {
                const vals = [];
                document.querySelectorAll(`[id^="${prefix}"]:checked`).forEach(cb => {
                    vals.push(cb.id.replace(prefix, '').replace('-', '.'));
                });
                return vals;
            };
            
            return {
                backend: currentBackend,
                tcpHost: document.getElementById('tcp-host').value,
                tcpCtrlPort: document.getElementById('tcp-ctrl-port').value,
                tcpDataPort: document.getElementById('tcp-data-port').value,
                workers: document.getElementById('num-workers').value,
                batchSize: document.getElementById('batch-size').value,
                parallelMode: document.getElementById('parallel-mode').value,
                duration: document.getElementById('duration').value,
                durationUnit: document.getElementById('duration-unit').value,
                seed: document.getElementById('rng-seed').value,
                modes: getModes(),
                categories: {
                    clean: document.getElementById('cat-clean').checked,
                    awgn: document.getElementById('cat-awgn').checked,
                    multipath: document.getElementById('cat-multipath').checked,
                    freqoff: document.getElementById('cat-freqoff').checked,
                    sizes: document.getElementById('cat-sizes').checked,
                    random: document.getElementById('cat-random').checked,
                    dfe: document.getElementById('cat-dfe').checked,
                    mlse: document.getElementById('cat-mlse').checked
                },
                snrLevels: getCheckedValues('snr-'),
                mpDelays: getCheckedValues('mp-'),
                echoGain: document.getElementById('echo-gain').value,
                freqOffsets: getCheckedValues('foff-'),
                testMessage: document.getElementById('test-message').value,
                msgSizes: getCheckedValues('size-'),
                generateReport: document.getElementById('opt-report').checked,
                exportCsv: document.getElementById('opt-csv').checked,
                verbose: document.getElementById('opt-verbose').checked,
                savePcm: document.getElementById('opt-savepcm').checked
            };
        }
        
        // Start test
        async function startTest() {
            if (testRunning) return;
            testRunning = true;
            
            document.getElementById('btn-start').style.display = 'none';
            document.getElementById('btn-stop').style.display = 'inline-block';
            document.getElementById('status-dot').className = 'status-dot running';
            document.getElementById('status-text').textContent = 'Running...';
            
            const config = gatherConfig();
            clearOutput();
            addOutput('==============================================', 'header');
            addOutput('M110A Exhaustive Test Suite', 'header');
            addOutput('==============================================', 'header');
            addOutput('Backend: ' + (config.backend === 'direct' ? 'Direct API' : 'TCP Server'), 'info');
            addOutput('Modes: ' + config.modes.join(', '), 'info');
            addOutput('Duration: ' + config.duration + ' ' + config.durationUnit, 'info');
            addOutput('', 'info');
            
            const params = new URLSearchParams();
            params.set('config', JSON.stringify(config));
            
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
                                if (data.output) addOutput(data.output, data.type || 'info');
                                if (data.tests !== undefined) document.getElementById('metric-tests').textContent = data.tests;
                                if (data.passed !== undefined) document.getElementById('metric-passed').textContent = data.passed;
                                if (data.rate !== undefined) document.getElementById('metric-rate').textContent = data.rate.toFixed(1) + '%';
                                if (data.currentTest) document.getElementById('current-test').textContent = data.currentTest;
                                if (data.progress !== undefined) document.getElementById('progress-fill').style.width = data.progress + '%';
                                if (data.elapsed) document.getElementById('elapsed-time').textContent = data.elapsed;
                                if (data.remaining) document.getElementById('remaining-time').textContent = data.remaining;
                                if (data.iteration !== undefined) document.getElementById('iteration').textContent = data.iteration;
                                if (data.done) testRunning = false;
                            } catch (e) {}
                        }
                    }
                }
            } catch (err) {
                addOutput('Error: ' + err.message, 'fail');
            }
            
            testRunning = false;
            document.getElementById('btn-start').style.display = 'inline-block';
            document.getElementById('btn-stop').style.display = 'none';
            document.getElementById('status-dot').className = 'status-dot';
            document.getElementById('status-text').textContent = 'Complete';
        }
        
        function stopTest() {
            testRunning = false;
            fetch('/stop-test');
        }
        
        function resetConfig() {
            document.getElementById('test-preset').value = 'standard';
            loadPreset();
            selectBackend('direct');
        }
        
        // Output
        function addOutput(text, type = 'info') {
            const output = document.getElementById('output');
            const line = document.createElement('div');
            line.className = 'output-line ' + type;
            line.textContent = text;
            output.appendChild(line);
            output.scrollTop = output.scrollHeight;
        }
        
        function clearOutput() { document.getElementById('output').innerHTML = ''; }
        function copyOutput() { navigator.clipboard.writeText(document.getElementById('output').innerText); }
        
        // Export
        function exportReport() { window.open('/export-report', '_blank'); }
        function exportCSV() { window.open('/export-csv', '_blank'); }
        function exportJSON() { window.open('/export-json', '_blank'); }
        
        // Interop functions
        function interopLog(msg, type = 'info') {
            const log = document.getElementById('interop-log');
            const time = new Date().toLocaleTimeString();
            log.innerHTML += `<div class="log-${type}">[${time}] ${msg}</div>`;
            log.scrollTop = log.scrollHeight;
        }
        
        async function togglePnServer() {
            const btn = document.getElementById('btn-pn-server');
            const dot = document.getElementById('pn-status-dot');
            const text = document.getElementById('pn-status-text');
            
            if (pnServerRunning) {
                const r = await fetch('/pn-server-stop');
                const d = await r.json();
                if (d.success) {
                    pnServerRunning = false;
                    dot.className = 'status-dot';
                    text.textContent = 'Server Stopped';
                    btn.textContent = 'Start Server';
                    btn.classList.remove('btn-disconnect');
                }
            } else {
                const ctrl = document.getElementById('pn-ctrl-port').value;
                const data = document.getElementById('pn-data-port').value;
                dot.className = 'status-dot running';
                text.textContent = 'Starting...';
                const r = await fetch(`/pn-server-start?ctrl=${ctrl}&data=${data}`);
                const d = await r.json();
                if (d.success) {
                    pnServerRunning = true;
                    dot.className = 'status-dot pass';
                    text.textContent = 'Running (PID: ' + d.pid + ')';
                    btn.textContent = 'Stop Server';
                    btn.classList.add('btn-disconnect');
                    interopLog('PhoenixNest server started', 'rx');
                } else {
                    dot.className = 'status-dot fail';
                    text.textContent = 'Failed';
                    interopLog('Start failed: ' + d.message, 'error');
                }
            }
        }
        
        async function toggleBrainConnection() {
            const btn = document.getElementById('btn-brain-connect');
            const dot = document.getElementById('brain-status-dot');
            const text = document.getElementById('brain-status-text');
            
            if (brainConnected) {
                await fetch('/brain-disconnect');
                brainConnected = false;
                dot.className = 'status-dot';
                text.textContent = 'Disconnected';
                btn.textContent = 'Connect';
                btn.classList.remove('btn-disconnect');
            } else {
                const host = document.getElementById('brain-host').value;
                const ctrl = document.getElementById('brain-ctrl-port').value;
                const data = document.getElementById('brain-data-port').value;
                dot.className = 'status-dot running';
                text.textContent = 'Connecting...';
                const r = await fetch(`/brain-connect?host=${encodeURIComponent(host)}&ctrl=${ctrl}&data=${data}`);
                const d = await r.json();
                if (d.success) {
                    brainConnected = true;
                    dot.className = 'status-dot pass';
                    text.textContent = 'Connected';
                    btn.textContent = 'Disconnect';
                    btn.classList.add('btn-disconnect');
                    interopLog('Connected to Brain modem', 'rx');
                } else {
                    dot.className = 'status-dot fail';
                    text.textContent = 'Failed';
                    interopLog('Connection failed: ' + d.message, 'error');
                }
            }
        }
        
        function runBrainToPnTest() { interopLog('Brain→PN test not yet implemented', 'info'); }
        function runPnToBrainTest() { interopLog('PN→Brain test not yet implemented', 'info'); }
        function runMatrix() { interopLog('Matrix test not yet implemented', 'info'); }
        
        // Reports
        async function loadReports() {
            const container = document.getElementById('reports-list');
            try {
                const r = await fetch('/list-reports');
                const d = await r.json();
                if (d.reports && d.reports.length > 0) {
                    container.innerHTML = d.reports.map(rep => 
                        `<div class="report-item"><div><span class="report-name">${rep.name}</span><br><span class="report-date">${rep.date} | ${rep.size}</span></div><a href="/report/${encodeURIComponent(rep.name)}" class="report-link" target="_blank">View →</a></div>`
                    ).join('');
                } else {
                    container.innerHTML = '<p style="color:#888;">No reports found.</p>';
                }
            } catch (e) {
                container.innerHTML = '<p style="color:#ff4757;">Error: ' + e.message + '</p>';
            }
        }
        
        // Initialize matrix table
        document.addEventListener('DOMContentLoaded', function() {
            const modes = ['75S','75L','150S','150L','300S','300L','600S','600L','1200S','1200L','2400S','2400L'];
            const tbody = document.getElementById('matrix-body');
            if (tbody) {
                tbody.innerHTML = modes.map(m => 
                    `<tr><td>${m}</td><td class="matrix-cell matrix-pending" id="cm-${m}-1">○</td><td class="matrix-cell matrix-pending" id="cm-${m}-2">○</td></tr>`
                ).join('');
            }
            
            // Populate mode dropdowns
            ['brain-pn-mode', 'pn-brain-mode'].forEach(id => {
                const sel = document.getElementById(id);
                if (sel) {
                    sel.innerHTML = modes.map(m => `<option value="${m}">${m}</option>`).join('');
                    sel.value = '600S';
                }
            });
        });
    </script>
</body>
</html>
)HTML";

} // namespace test_gui
