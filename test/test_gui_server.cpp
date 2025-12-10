/**
 * @file test_gui_server.cpp
 * @brief Web-based GUI for M110A Exhaustive Test Suite
 * 
 * Provides a simple HTTP server that serves a web UI for running tests.
 * Launches the unified exhaustive_test.exe and streams output to browser.
 * 
 * FIXED VERSION - Includes:
 * - Proper PhoenixNest server status checking in all interop handlers
 * - Brain modem support for cross-modem testing
 * - Better error messages for server not running
 * 
 * Usage:
 *   test_gui.exe [--port N]
 *   Then open http://localhost:8080 in browser
 */

#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
#include <tlhelp32.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket close
#endif

#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <thread>
#include <vector>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <regex>
#include <random>
#include <ctime>
#include <chrono>

#include "common/license.h"

namespace fs = std::filesystem;

#ifdef _WIN32
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

// HTML page with embedded JavaScript - FIXED VERSION with Brain modem support
const char* HTML_PAGE = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <title>M110A Modem Test Suite</title>
    <style>
        body { font-family: 'Segoe UI', Arial, sans-serif; margin: 20px; background: #1a1a2e; color: #eee; }
        h1 { color: #00d4ff; }
        .container { max-width: 1200px; margin: 0 auto; }
        .tabs { display: flex; gap: 5px; margin-bottom: 0; }
        .tab { padding: 12px 25px; background: #16213e; border: none; border-radius: 8px 8px 0 0;
               color: #aaa; cursor: pointer; font-weight: bold; }
        .tab.active { background: #16213e; color: #00d4ff; border-bottom: 2px solid #00d4ff; }
        .tab:hover { color: #00d4ff; }
        .tab-content { display: none; }
        .tab-content.active { display: block; }
        .controls { background: #16213e; padding: 20px; border-radius: 0 8px 8px 8px; margin-bottom: 20px; }
        .row { display: flex; gap: 20px; margin-bottom: 15px; flex-wrap: wrap; }
        .field { display: flex; flex-direction: column; }
        label { margin-bottom: 5px; color: #aaa; font-size: 12px; }
        select, input { padding: 8px 12px; border: 1px solid #333; border-radius: 4px; 
                       background: #0f0f23; color: #fff; min-width: 120px; }
        select[multiple] { height: 180px; min-width: 160px; }
        select[multiple] option { padding: 4px 8px; }
        select[multiple] option:checked { background: #00d4ff; color: #000; }
        .select-hint { font-size: 10px; color: #666; margin-top: 3px; }
        button { padding: 10px 25px; border: none; border-radius: 4px; cursor: pointer; 
                font-weight: bold; margin-right: 10px; }
        .test-summary { background: #0f3460; padding: 10px 15px; border-radius: 4px; 
                       margin-bottom: 15px; font-size: 13px; color: #aaa; }
        .test-summary strong { color: #00d4ff; }
        .btn-run { background: #00d4ff; color: #000; }
        .btn-run:hover { background: #00a8cc; }
        .btn-run:disabled { background: #444; color: #888; cursor: not-allowed; }
        .btn-stop { background: #ff4757; color: #fff; }
        .btn-stop:hover { background: #cc3a47; }
        .btn-refresh { background: #5f5f1e; color: #fff; }
        .btn-refresh:hover { background: #7a7a25; }
        .output { background: #0f0f23; border: 1px solid #333; border-radius: 8px; 
                 padding: 15px; height: 500px; overflow-y: auto; font-family: 'Consolas', monospace;
                 font-size: 13px; white-space: pre-wrap; }
        .status { padding: 10px; border-radius: 4px; margin-bottom: 15px; }
        .status-idle { background: #333; }
        .status-running { background: #1e3a5f; }
        .status-pass { background: #1e5f3a; }
        .status-fail { background: #5f1e1e; }
        .checkbox-group { display: flex; gap: 15px; align-items: center; }
        .checkbox-group label { display: flex; align-items: center; gap: 5px; cursor: pointer; }
        .checkbox-group input[type="checkbox"] { width: 16px; height: 16px; }
        .progress { height: 4px; background: #333; border-radius: 2px; margin-top: 10px; overflow: hidden; }
        .progress-bar { height: 100%; background: #00d4ff; width: 0%; transition: width 0.3s; }
        
        /* Brain Modem Interop styles */
        .interop-section { background: #0f3460; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
        .interop-section h3 { color: #00d4ff; margin: 0 0 15px 0; display: flex; align-items: center; gap: 10px; }
        .interop-config { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin-bottom: 15px; }
        .interop-field { display: flex; flex-direction: column; }
        .interop-field label { font-size: 12px; color: #aaa; margin-bottom: 5px; }
        .interop-field input { padding: 8px 12px; border: 1px solid #333; border-radius: 4px; 
                               background: #0f0f23; color: #fff; }
        .interop-status { display: flex; align-items: center; gap: 10px; padding: 10px 15px; 
                          background: #16213e; border-radius: 4px; margin-bottom: 15px; }
        .status-dot { width: 12px; height: 12px; border-radius: 50%; }
        .status-dot.disconnected { background: #ff4757; }
        .status-dot.connecting { background: #ff9f43; animation: pulse 1s infinite; }
        .status-dot.connected { background: #5fff5f; }
        .btn-connect { background: #00d4ff; color: #000; padding: 10px 20px; border: none; 
                       border-radius: 4px; cursor: pointer; font-weight: bold; }
        .btn-connect:hover { background: #00a8cc; }
        .btn-connect:disabled { background: #444; color: #888; cursor: not-allowed; }
        .btn-disconnect { background: #ff4757; color: #fff; }
        .btn-disconnect:hover { background: #cc3a47; }
        .test-direction { background: #16213e; padding: 20px; border-radius: 8px; margin-bottom: 15px; }
        .test-direction h4 { color: #fff; margin: 0 0 15px 0; }
        .test-controls { display: flex; gap: 15px; align-items: center; flex-wrap: wrap; margin-bottom: 15px; }
        .test-steps { list-style: none; padding: 0; margin: 0; }
        .test-steps li { padding: 8px 0; display: flex; align-items: center; gap: 10px; 
                        border-bottom: 1px solid #333; font-size: 13px; }
        .test-steps li:last-child { border-bottom: none; }
        .step-icon { width: 20px; text-align: center; }
        .step-pending { color: #666; }
        .step-running { color: #ff9f43; }
        .step-complete { color: #5fff5f; }
        .step-error { color: #ff4757; }
        .test-result { padding: 10px 15px; border-radius: 4px; margin-top: 15px; }
        .test-result.success { background: #1e5f3a; }
        .test-result.failure { background: #5f1e1e; }
        .test-result.pending { background: #333; color: #888; }
        .matrix-container { background: #16213e; padding: 20px; border-radius: 8px; }
        .matrix-table { width: 100%; border-collapse: collapse; }
        .matrix-table th, .matrix-table td { padding: 10px; text-align: center; border: 1px solid #333; }
        .matrix-table th { background: #0f3460; color: #00d4ff; }
        .matrix-table td { background: #0f0f23; }
        .matrix-cell { font-size: 16px; }
        .matrix-pass { color: #5fff5f; }
        .matrix-fail { color: #ff4757; }
        .matrix-pending { color: #666; }
        .matrix-running { color: #ff9f43; animation: pulse 1s infinite; }
        .interop-log { background: #0f0f23; border: 1px solid #333; border-radius: 4px; 
                       padding: 10px; height: 200px; overflow-y: auto; font-family: 'Consolas', monospace;
                       font-size: 12px; margin-top: 15px; }
        .log-tx { color: #ff9f43; }
        .log-rx { color: #5fff5f; }
        .log-info { color: #aaa; }
        .log-error { color: #ff4757; }
        
        /* Sub-tab navigation for Interop */
        .sub-tabs { display: flex; gap: 10px; margin-bottom: 20px; flex-wrap: wrap; }
        .sub-tab { padding: 10px 20px; border: 1px solid #333; border-radius: 20px; 
                   background: #16213e; color: #888; cursor: pointer; font-size: 13px;
                   transition: all 0.2s ease; }
        .sub-tab:hover { background: #1e3a5f; color: #fff; }
        .sub-tab.active { background: #00d4ff; color: #000; border-color: #00d4ff; font-weight: bold; }
        .sub-tab-content { display: none; }
        .sub-tab-content.active { display: block; }
        
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.7; } }
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
        
        <div id="tab-tests" class="tab-content active">
            <div class="controls">
                <div class="row">
                    <div class="field">
                        <label>Test Type</label>
                        <select id="test-type">
                            <option value="loopback">Loopback Test</option>
                            <option value="reference">Reference PCM Test</option>
                        </select>
                    </div>
                    <div class="field">
                        <label>Modes</label>
                        <select id="modes" multiple>
                            <option value="150S">150S</option>
                            <option value="150L">150L</option>
                            <option value="300S">300S</option>
                            <option value="300L">300L</option>
                            <option value="600S" selected>600S</option>
                            <option value="600L">600L</option>
                            <option value="1200S">1200S</option>
                            <option value="1200L">1200L</option>
                            <option value="2400S">2400S</option>
                            <option value="2400L">2400L</option>
                        </select>
                    </div>
                </div>
                <button class="btn-run" onclick="runTest()">Run Test</button>
            </div>
            <div class="output" id="output">Ready to run tests...</div>
        </div>
        
        <div id="tab-interop" class="tab-content">
            <div class="controls">
                <!-- Sub-tab Navigation -->
                <div class="sub-tabs">
                    <button class="sub-tab active" onclick="showSubTab('setup')">ðŸ”§ Connection Setup</button>
                    <button class="sub-tab" onclick="showSubTab('brain-pn')">ðŸ§  Brain â†’ PhoenixNest</button>
                    <button class="sub-tab" onclick="showSubTab('pn-brain')">ðŸš€ PhoenixNest â†’ Brain</button>
                    <button class="sub-tab" onclick="showSubTab('matrix')">ðŸ“Š Full Matrix</button>
                </div>
                
                <!-- Sub-tab: Connection Setup -->
                <div id="subtab-setup" class="sub-tab-content active">
                    <!-- PhoenixNest Server -->
                    <div class="interop-section">
                        <h3>ðŸš€ PhoenixNest Server (m110a_server.exe)</h3>
                        <p style="color:#aaa; margin-bottom:15px; font-size:13px;">
                            Start the PhoenixNest M110A modem server for interoperability testing.
                        </p>
                        <div class="interop-config">
                            <div class="interop-field">
                                <label>Control Port</label>
                                <input type="number" id="pn-ctrl-port" value="5100" />
                            </div>
                            <div class="interop-field">
                                <label>Data Port</label>
                                <input type="number" id="pn-data-port" value="5101" />
                            </div>
                        </div>
                        <div class="interop-status">
                            <span class="status-dot disconnected" id="pn-status-dot"></span>
                            <span id="pn-status-text">Server Stopped</span>
                            <button class="btn-connect" id="btn-pn-server" onclick="togglePhoenixNestServer()">
                                Start Server
                            </button>
                        </div>
                    </div>
                    
                    <!-- Brain Modem Server -->
                    <div class="interop-section">
                        <h3>ðŸ§  Paul Brain Modem Server (brain_modem_server.exe)</h3>
                        <p style="color:#aaa; margin-bottom:15px; font-size:13px;">
                            Connect to the Paul Brain modem server for cross-modem testing.
                        </p>
                        <div class="interop-config">
                            <div class="interop-field">
                                <label>Host</label>
                                <input type="text" id="brain-host" value="localhost" />
                            </div>
                            <div class="interop-field">
                                <label>Control Port</label>
                                <input type="number" id="brain-ctrl-port" value="3999" />
                            </div>
                            <div class="interop-field">
                                <label>Data Port</label>
                                <input type="number" id="brain-data-port" value="3998" />
                            </div>
                        </div>
                        <div class="interop-status">
                            <span class="status-dot disconnected" id="brain-status-dot"></span>
                            <span id="brain-status-text">Disconnected</span>
                            <button class="btn-connect" id="btn-brain-connect" onclick="toggleBrainConnection()">
                                Connect to Brain
                            </button>
                        </div>
                    </div>
                </div>
                
                <!-- Sub-tab: Brain TX â†’ PhoenixNest RX -->
                <div id="subtab-brain-pn" class="sub-tab-content">
                    <div class="test-direction">
                        <h4>ðŸ§ ðŸ“¤ Brain TX â†’ ðŸš€ðŸ“¥ PhoenixNest RX</h4>
                        <p style="color:#888; font-size:12px; margin-bottom:15px;">
                            Paul Brain modem transmits, PhoenixNest modem receives. Tests Brain TX compatibility.
                        </p>
                        <div class="test-controls">
                            <div class="field">
                                <label>Mode</label>
                                <select id="brain-pn-mode">
                                    <option value="150S">150 bps Short</option>
                                    <option value="150L">150 bps Long</option>
                                    <option value="300S">300 bps Short</option>
                                    <option value="300L">300 bps Long</option>
                                    <option value="600S" selected>600 bps Short</option>
                                    <option value="600L">600 bps Long</option>
                                    <option value="1200S">1200 bps Short</option>
                                    <option value="1200L">1200 bps Long</option>
                                    <option value="2400S">2400 bps Short</option>
                                    <option value="2400L">2400 bps Long</option>
                                </select>
                            </div>
                            <div class="field">
                                <label>Test Message</label>
                                <input type="text" id="brain-pn-msg" value="HELLO CROSS MODEM TEST" style="width:250px;" />
                            </div>
                            <button class="btn-run" id="btn-brain-pn" onclick="runBrainToPnTest()">
                                â–¶ Run Test
                            </button>
                        </div>
                        <ul class="test-steps" id="brain-pn-steps">
                            <li><span class="step-icon step-pending">â—‹</span> Set Brain data rate</li>
                            <li><span class="step-icon step-pending">â—‹</span> Enable Brain TX recording</li>
                            <li><span class="step-icon step-pending">â—‹</span> Send test message to Brain</li>
                            <li><span class="step-icon step-pending">â—‹</span> Trigger Brain SENDBUFFER</li>
                            <li><span class="step-icon step-pending">â—‹</span> Wait for Brain TX:COMPLETE</li>
                            <li><span class="step-icon step-pending">â—‹</span> Find Brain TX PCM file</li>
                            <li><span class="step-icon step-pending">â—‹</span> Inject PCM into PhoenixNest RX</li>
                            <li><span class="step-icon step-pending">â—‹</span> Wait for PhoenixNest DCD</li>
                            <li><span class="step-icon step-pending">â—‹</span> Read PhoenixNest decoded data</li>
                            <li><span class="step-icon step-pending">â—‹</span> Compare output</li>
                        </ul>
                        <div class="test-result pending" id="brain-pn-result">
                            Result will appear here after test completes
                        </div>
                    </div>
                </div>
                
                <!-- Sub-tab: PhoenixNest TX â†’ Brain RX -->
                <div id="subtab-pn-brain" class="sub-tab-content">
                    <div class="test-direction">
                        <h4>ðŸš€ðŸ“¤ PhoenixNest TX â†’ ðŸ§ ðŸ“¥ Brain RX</h4>
                        <p style="color:#888; font-size:12px; margin-bottom:15px;">
                            PhoenixNest modem transmits, Brain modem receives. Tests PhoenixNest TX compatibility.
                        </p>
                        <div class="test-controls">
                            <div class="field">
                                <label>Mode</label>
                                <select id="pn-brain-mode">
                                    <option value="150S">150 bps Short</option>
                                    <option value="150L">150 bps Long</option>
                                    <option value="300S">300 bps Short</option>
                                    <option value="300L">300 bps Long</option>
                                    <option value="600S" selected>600 bps Short</option>
                                    <option value="600L">600 bps Long</option>
                                    <option value="1200S">1200 bps Short</option>
                                    <option value="1200L">1200 bps Long</option>
                                    <option value="2400S">2400 bps Short</option>
                                    <option value="2400L">2400 bps Long</option>
                                </select>
                            </div>
                            <div class="field">
                                <label>Test Message</label>
                                <input type="text" id="pn-brain-msg" value="HELLO CROSS MODEM TEST" style="width:250px;" />
                            </div>
                            <button class="btn-run" id="btn-pn-brain" onclick="runPnToBrainTest()">
                                â–¶ Run Test
                            </button>
                        </div>
                        <ul class="test-steps" id="pn-brain-steps">
                            <li><span class="step-icon step-pending">â—‹</span> Set PhoenixNest data rate</li>
                            <li><span class="step-icon step-pending">â—‹</span> Enable PhoenixNest TX recording</li>
                            <li><span class="step-icon step-pending">â—‹</span> Send test message to PhoenixNest</li>
                            <li><span class="step-icon step-pending">â—‹</span> Trigger PhoenixNest SENDBUFFER</li>
                            <li><span class="step-icon step-pending">â—‹</span> Wait for PhoenixNest TX:IDLE</li>
                            <li><span class="step-icon step-pending">â—‹</span> Get PhoenixNest TX PCM file</li>
                            <li><span class="step-icon step-pending">â—‹</span> Inject PCM into Brain RX</li>
                            <li><span class="step-icon step-pending">â—‹</span> Wait for Brain DCD</li>
                            <li><span class="step-icon step-pending">â—‹</span> Read Brain decoded data</li>
                            <li><span class="step-icon step-pending">â—‹</span> Compare output</li>
                        </ul>
                        <div class="test-result pending" id="pn-brain-result">
                            Result will appear here after test completes
                        </div>
                    </div>
                </div>
                
                <!-- Sub-tab: Full Matrix -->
                <div id="subtab-matrix" class="sub-tab-content">
                    <div class="matrix-container">
                        <h3 style="color:#00d4ff; margin:0 0 15px 0;">ðŸ“Š Cross-Modem Compatibility Matrix</h3>
                        <p style="color:#888; font-size:12px; margin-bottom:15px;">
                            Full compatibility test between Paul Brain and PhoenixNest modems.
                        </p>
                        <div class="test-controls" style="margin-bottom:15px;">
                            <button class="btn-run" id="btn-matrix" onclick="runCrossModemMatrix()">
                                â–¶ Run All Tests (20 total)
                            </button>
                            <span id="matrix-progress" style="color:#aaa;">Progress: 0/20</span>
                        </div>
                        <table class="matrix-table">
                            <thead>
                                <tr>
                                    <th>Mode</th>
                                    <th>Brain â†’ PN</th>
                                    <th>PN â†’ Brain</th>
                                </tr>
                            </thead>
                            <tbody id="cross-matrix-body">
                                <tr><td>150S</td><td class="matrix-cell matrix-pending" id="cm-150S-1">â—‹</td><td class="matrix-cell matrix-pending" id="cm-150S-2">â—‹</td></tr>
                                <tr><td>150L</td><td class="matrix-cell matrix-pending" id="cm-150L-1">â—‹</td><td class="matrix-cell matrix-pending" id="cm-150L-2">â—‹</td></tr>
                                <tr><td>300S</td><td class="matrix-cell matrix-pending" id="cm-300S-1">â—‹</td><td class="matrix-cell matrix-pending" id="cm-300S-2">â—‹</td></tr>
                                <tr><td>300L</td><td class="matrix-cell matrix-pending" id="cm-300L-1">â—‹</td><td class="matrix-cell matrix-pending" id="cm-300L-2">â—‹</td></tr>
                                <tr><td>600S</td><td class="matrix-cell matrix-pending" id="cm-600S-1">â—‹</td><td class="matrix-cell matrix-pending" id="cm-600S-2">â—‹</td></tr>
                                <tr><td>600L</td><td class="matrix-cell matrix-pending" id="cm-600L-1">â—‹</td><td class="matrix-cell matrix-pending" id="cm-600L-2">â—‹</td></tr>
                                <tr><td>1200S</td><td class="matrix-cell matrix-pending" id="cm-1200S-1">â—‹</td><td class="matrix-cell matrix-pending" id="cm-1200S-2">â—‹</td></tr>
                                <tr><td>1200L</td><td class="matrix-cell matrix-pending" id="cm-1200L-1">â—‹</td><td class="matrix-cell matrix-pending" id="cm-1200L-2">â—‹</td></tr>
                                <tr><td>2400S</td><td class="matrix-cell matrix-pending" id="cm-2400S-1">â—‹</td><td class="matrix-cell matrix-pending" id="cm-2400S-2">â—‹</td></tr>
                                <tr><td>2400L</td><td class="matrix-cell matrix-pending" id="cm-2400L-1">â—‹</td><td class="matrix-cell matrix-pending" id="cm-2400L-2">â—‹</td></tr>
                            </tbody>
                        </table>
                    </div>
                </div>
                
                <div class="interop-log" id="interop-log">
                    <div class="log-info">[INFO] Cross-Modem Interop Test Log</div>
                    <div class="log-info">[INFO] Start PhoenixNest server and connect to Brain modem to begin testing</div>
                </div>
            </div>
        </div>
        
        <div id="tab-reports" class="tab-content">
            <div class="controls">
                <h2 style="color:#00d4ff;">Test Reports</h2>
                <div id="reports-list">Loading reports...</div>
            </div>
        </div>
    </div>
    
    <script>
        let pnServerRunning = false;
        let brainConnected = false;
        let interopTestRunning = false;
        
        function showTab(tabName) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            document.querySelector('.tab[onclick*="' + tabName + '"]').classList.add('active');
            document.getElementById('tab-' + tabName).classList.add('active');
        }
        
        function showSubTab(subTabName) {
            document.querySelectorAll('.sub-tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.sub-tab-content').forEach(t => t.classList.remove('active'));
            document.querySelector('.sub-tab[onclick*="' + subTabName + '"]').classList.add('active');
            document.getElementById('subtab-' + subTabName).classList.add('active');
        }
        
        function interopLog(message, type = 'info') {
            const log = document.getElementById('interop-log');
            const timestamp = new Date().toLocaleTimeString();
            const className = 'log-' + type;
            log.innerHTML += '<div class="' + className + '">[' + timestamp + '] ' + message + '</div>';
            log.scrollTop = log.scrollHeight;
        }
        
        // ============ PHOENIXNEST SERVER ============
        async function togglePhoenixNestServer() {
            const btn = document.getElementById('btn-pn-server');
            const dot = document.getElementById('pn-status-dot');
            const text = document.getElementById('pn-status-text');
            
            if (pnServerRunning) {
                // Stop server
                dot.className = 'status-dot connecting';
                text.textContent = 'Stopping...';
                btn.disabled = true;
                interopLog('Stopping PhoenixNest server...', 'info');
                
                try {
                    const response = await fetch('/pn-server-stop');
                    const result = await response.json();
                    
                    if (result.success) {
                        pnServerRunning = false;
                        dot.className = 'status-dot disconnected';
                        text.textContent = 'Server Stopped';
                        btn.textContent = 'Start Server';
                        btn.classList.remove('btn-disconnect');
                        interopLog('PhoenixNest server stopped', 'info');
                    } else {
                        dot.className = 'status-dot connected';
                        text.textContent = 'Running';
                        interopLog('Failed to stop server: ' + result.message, 'error');
                    }
                } catch (err) {
                    interopLog('Stop error: ' + err.message, 'error');
                }
                btn.disabled = false;
            } else {
                // Start server
                const ctrlPort = document.getElementById('pn-ctrl-port').value;
                const dataPort = document.getElementById('pn-data-port').value;
                
                dot.className = 'status-dot connecting';
                text.textContent = 'Starting...';
                btn.disabled = true;
                interopLog('Starting PhoenixNest server on ports ' + ctrlPort + '/' + dataPort + '...', 'info');
                
                try {
                    const response = await fetch('/pn-server-start?ctrl=' + ctrlPort + '&data=' + dataPort);
                    const result = await response.json();
                    
                    if (result.success) {
                        pnServerRunning = true;
                        dot.className = 'status-dot connected';
                        text.textContent = 'Running (PID: ' + result.pid + ')';
                        btn.textContent = 'Stop Server';
                        btn.classList.add('btn-disconnect');
                        interopLog('PhoenixNest server started: PID ' + result.pid, 'rx');
                    } else {
                        dot.className = 'status-dot disconnected';
                        text.textContent = 'Failed to start';
                        interopLog('Failed to start server: ' + result.message, 'error');
                    }
                } catch (err) {
                    dot.className = 'status-dot disconnected';
                    text.textContent = 'Start error';
                    interopLog('Start error: ' + err.message, 'error');
                }
                btn.disabled = false;
            }
        }
        
        // Check server status on page load
        async function checkPnServerStatus() {
            try {
                const response = await fetch('/pn-server-status');
                const result = await response.json();
                
                if (result.running) {
                    pnServerRunning = true;
                    document.getElementById('pn-status-dot').className = 'status-dot connected';
                    document.getElementById('pn-status-text').textContent = 'Running (PID: ' + result.pid + ')';
                    document.getElementById('btn-pn-server').textContent = 'Stop Server';
                    document.getElementById('btn-pn-server').classList.add('btn-disconnect');
                }
            } catch (err) {
                console.log('Server status check failed:', err);
            }
        }
        
        // ============ BRAIN MODEM CONNECTION ============
        async function toggleBrainConnection() {
            const btn = document.getElementById('btn-brain-connect');
            const dot = document.getElementById('brain-status-dot');
            const text = document.getElementById('brain-status-text');
            
            if (brainConnected) {
                // Disconnect
                try {
                    await fetch('/brain-disconnect');
                    brainConnected = false;
                    dot.className = 'status-dot disconnected';
                    text.textContent = 'Disconnected';
                    btn.textContent = 'Connect to Brain';
                    btn.classList.remove('btn-disconnect');
                    interopLog('Disconnected from Brain modem', 'info');
                } catch (err) {
                    interopLog('Disconnect error: ' + err.message, 'error');
                }
            } else {
                // Connect
                const host = document.getElementById('brain-host').value;
                const ctrlPort = document.getElementById('brain-ctrl-port').value;
                const dataPort = document.getElementById('brain-data-port').value;
                
                dot.className = 'status-dot connecting';
                text.textContent = 'Connecting...';
                btn.disabled = true;
                interopLog('Connecting to Brain modem at ' + host + ':' + ctrlPort + '/' + dataPort + '...', 'info');
                
                try {
                    const response = await fetch('/brain-connect?host=' + encodeURIComponent(host) + 
                        '&ctrl=' + ctrlPort + '&data=' + dataPort);
                    const result = await response.json();
                    
                    if (result.success) {
                        brainConnected = true;
                        dot.className = 'status-dot connected';
                        text.textContent = 'Connected - ' + (result.message || 'MODEM READY');
                        btn.textContent = 'Disconnect';
                        btn.classList.add('btn-disconnect');
                        interopLog('Connected to Brain modem: ' + result.message, 'rx');
                    } else {
                        dot.className = 'status-dot disconnected';
                        text.textContent = 'Connection failed';
                        interopLog('Connection failed: ' + result.message, 'error');
                    }
                } catch (err) {
                    dot.className = 'status-dot disconnected';
                    text.textContent = 'Connection error';
                    interopLog('Connection error: ' + err.message, 'error');
                }
                btn.disabled = false;
            }
        }
        
        // ============ CROSS-MODEM TESTS ============
        function updateTestStep(testId, stepIndex, status) {
            const steps = document.getElementById(testId + '-steps').children;
            if (stepIndex < steps.length) {
                const icon = steps[stepIndex].querySelector('.step-icon');
                icon.className = 'step-icon step-' + status;
                if (status === 'pending') icon.textContent = 'â—‹';
                else if (status === 'running') icon.textContent = 'â—';
                else if (status === 'complete') icon.textContent = 'âœ“';
                else if (status === 'error') icon.textContent = 'âœ—';
            }
        }
        
        function resetTestSteps(testId, count) {
            for (let i = 0; i < count; i++) {
                updateTestStep(testId, i, 'pending');
            }
            const result = document.getElementById(testId + '-result');
            result.className = 'test-result pending';
            result.textContent = 'Result will appear here after test completes';
        }
        
        async function runBrainToPnTest() {
            // Check prerequisites
            if (!brainConnected) {
                interopLog('Brain modem not connected - connect first in Connection Setup', 'error');
                alert('Please connect to Brain modem first in the Connection Setup tab');
                return;
            }
            if (!pnServerRunning) {
                interopLog('PhoenixNest server not running - start it first in Connection Setup', 'error');
                alert('Please start PhoenixNest server first in the Connection Setup tab');
                return;
            }
            
            if (interopTestRunning) return;
            interopTestRunning = true;
            
            const mode = document.getElementById('brain-pn-mode').value;
            const message = document.getElementById('brain-pn-msg').value;
            
            document.getElementById('btn-brain-pn').disabled = true;
            resetTestSteps('brain-pn', 10);
            interopLog('Starting Brain TX â†’ PhoenixNest RX test, Mode: ' + mode, 'info');
            
            try {
                const response = await fetch('/brain-to-pn-test?mode=' + encodeURIComponent(mode) + 
                    '&message=' + encodeURIComponent(message));
                
                const reader = response.body.getReader();
                const decoder = new TextDecoder();
                
                while (true) {
                    const { value, done } = await reader.read();
                    if (done) break;
                    
                    const text = decoder.decode(value);
                    const lines = text.split('\n');
                    
                    for (const line of lines) {
                        if (line.startsWith('data: ')) {
                            try {
                                const data = JSON.parse(line.substring(6));
                                if (data.step !== undefined) {
                                    updateTestStep('brain-pn', data.step, data.status);
                                }
                                if (data.log) {
                                    interopLog(data.log, data.logType || 'info');
                                }
                                if (data.result) {
                                    const result = document.getElementById('brain-pn-result');
                                    result.className = 'test-result ' + (data.success ? 'success' : 'failure');
                                    result.textContent = data.result;
                                }
                            } catch (e) {}
                        }
                    }
                }
            } catch (err) {
                interopLog('Test error: ' + err.message, 'error');
                document.getElementById('brain-pn-result').className = 'test-result failure';
                document.getElementById('brain-pn-result').textContent = 'Error: ' + err.message;
            }
            
            document.getElementById('btn-brain-pn').disabled = false;
            interopTestRunning = false;
        }
        
        async function runPnToBrainTest() {
            // Check prerequisites
            if (!pnServerRunning) {
                interopLog('PhoenixNest server not running - start it first in Connection Setup', 'error');
                alert('Please start PhoenixNest server first in the Connection Setup tab');
                return;
            }
            if (!brainConnected) {
                interopLog('Brain modem not connected - connect first in Connection Setup', 'error');
                alert('Please connect to Brain modem first in the Connection Setup tab');
                return;
            }
            
            if (interopTestRunning) return;
            interopTestRunning = true;
            
            const mode = document.getElementById('pn-brain-mode').value;
            const message = document.getElementById('pn-brain-msg').value;
            
            document.getElementById('btn-pn-brain').disabled = true;
            resetTestSteps('pn-brain', 10);
            interopLog('Starting PhoenixNest TX â†’ Brain RX test, Mode: ' + mode, 'info');
            
            try {
                const response = await fetch('/pn-to-brain-test?mode=' + encodeURIComponent(mode) + 
                    '&message=' + encodeURIComponent(message));
                
                const reader = response.body.getReader();
                const decoder = new TextDecoder();
                
                while (true) {
                    const { value, done } = await reader.read();
                    if (done) break;
                    
                    const text = decoder.decode(value);
                    const lines = text.split('\n');
                    
                    for (const line of lines) {
                        if (line.startsWith('data: ')) {
                            try {
                                const data = JSON.parse(line.substring(6));
                                if (data.step !== undefined) {
                                    updateTestStep('pn-brain', data.step, data.status);
                                }
                                if (data.log) {
                                    interopLog(data.log, data.logType || 'info');
                                }
                                if (data.result) {
                                    const result = document.getElementById('pn-brain-result');
                                    result.className = 'test-result ' + (data.success ? 'success' : 'failure');
                                    result.textContent = data.result;
                                }
                            } catch (e) {}
                        }
                    }
                }
            } catch (err) {
                interopLog('Test error: ' + err.message, 'error');
                document.getElementById('pn-brain-result').className = 'test-result failure';
                document.getElementById('pn-brain-result').textContent = 'Error: ' + err.message;
            }
            
            document.getElementById('btn-pn-brain').disabled = false;
            interopTestRunning = false;
        }
        
        async function runCrossModemMatrix() {
            // Check prerequisites
            if (!brainConnected) {
                interopLog('Brain modem not connected', 'error');
                alert('Please connect to Brain modem first');
                return;
            }
            if (!pnServerRunning) {
                interopLog('PhoenixNest server not running', 'error');
                alert('Please start PhoenixNest server first');
                return;
            }
            
            if (interopTestRunning) return;
            interopTestRunning = true;
            
            const modes = ['150S', '150L', '300S', '300L', '600S', '600L', '1200S', '1200L', '2400S', '2400L'];
            const message = 'CROSS MODEM MATRIX TEST';
            let completed = 0;
            const total = modes.length * 2;
            
            document.getElementById('btn-matrix').disabled = true;
            
            // Reset all cells
            for (const mode of modes) {
                document.getElementById('cm-' + mode + '-1').className = 'matrix-cell matrix-pending';
                document.getElementById('cm-' + mode + '-1').textContent = 'â—‹';
                document.getElementById('cm-' + mode + '-2').className = 'matrix-cell matrix-pending';
                document.getElementById('cm-' + mode + '-2').textContent = 'â—‹';
            }
            
            interopLog('Starting cross-modem matrix test (20 tests)', 'info');
            
            for (const mode of modes) {
                // Test 1: Brain TX â†’ PN RX
                document.getElementById('cm-' + mode + '-1').className = 'matrix-cell matrix-running';
                document.getElementById('cm-' + mode + '-1').textContent = 'â—';
                
                try {
                    const resp1 = await fetch('/brain-to-pn-quick?mode=' + mode + '&message=' + encodeURIComponent(message));
                    const result1 = await resp1.json();
                    
                    const cell1 = document.getElementById('cm-' + mode + '-1');
                    cell1.className = 'matrix-cell ' + (result1.success ? 'matrix-pass' : 'matrix-fail');
                    cell1.textContent = result1.success ? 'âœ“' : 'âœ—';
                    interopLog(mode + ' Brainâ†’PN: ' + (result1.success ? 'PASS' : 'FAIL - ' + (result1.error || 'Unknown')), 
                              result1.success ? 'rx' : 'error');
                } catch (err) {
                    const cell1 = document.getElementById('cm-' + mode + '-1');
                    cell1.className = 'matrix-cell matrix-fail';
                    cell1.textContent = 'âœ—';
                    interopLog(mode + ' Brainâ†’PN: ERROR - ' + err.message, 'error');
                }
                completed++;
                document.getElementById('matrix-progress').textContent = 'Progress: ' + completed + '/' + total;
                
                // Test 2: PN TX â†’ Brain RX
                document.getElementById('cm-' + mode + '-2').className = 'matrix-cell matrix-running';
                document.getElementById('cm-' + mode + '-2').textContent = 'â—';
                
                try {
                    const resp2 = await fetch('/pn-to-brain-quick?mode=' + mode + '&message=' + encodeURIComponent(message));
                    const result2 = await resp2.json();
                    
                    const cell2 = document.getElementById('cm-' + mode + '-2');
                    cell2.className = 'matrix-cell ' + (result2.success ? 'matrix-pass' : 'matrix-fail');
                    cell2.textContent = result2.success ? 'âœ“' : 'âœ—';
                    interopLog(mode + ' PNâ†’Brain: ' + (result2.success ? 'PASS' : 'FAIL - ' + (result2.error || 'Unknown')), 
                              result2.success ? 'rx' : 'error');
                } catch (err) {
                    const cell2 = document.getElementById('cm-' + mode + '-2');
                    cell2.className = 'matrix-cell matrix-fail';
                    cell2.textContent = 'âœ—';
                    interopLog(mode + ' PNâ†’Brain: ERROR - ' + err.message, 'error');
                }
                completed++;
                document.getElementById('matrix-progress').textContent = 'Progress: ' + completed + '/' + total;
            }
            
            interopLog('Cross-modem matrix test complete', 'info');
            document.getElementById('btn-matrix').disabled = false;
            interopTestRunning = false;
        }
        
        function runTest() {
            // Simple test runner placeholder
            const output = document.getElementById('output');
            output.textContent = 'Running test...\n';
        }
        
        // Initialize on page load
        document.addEventListener('DOMContentLoaded', function() {
            checkPnServerStatus();
        });
    </script>
</body>
</html>
)HTML";

class TestGuiServer {
public:
    TestGuiServer(int port = 8080) : port_(port), running_(false), test_process_(nullptr) {
#ifdef _WIN32
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        exe_dir_ = path;
        size_t last_slash = exe_dir_.find_last_of("\\/");
        if (last_slash != std::string::npos) {
            exe_dir_ = exe_dir_.substr(0, last_slash + 1);
        }
#else
        exe_dir_ = "./";
#endif
        std::random_device rd;
        std::mt19937_64 gen(rd() ^ std::time(nullptr));
        std::uniform_int_distribution<uint64_t> dis;
        std::ostringstream oss;
        oss << std::hex << dis(gen) << dis(gen);
        session_token_ = oss.str();
    }
    
    bool start() {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            std::cerr << "WSAStartup failed\n";
            return false;
        }
#endif
        
        server_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock_ == INVALID_SOCKET) {
            std::cerr << "Socket creation failed\n";
            return false;
        }
        
        int opt = 1;
        setsockopt(server_sock_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        
        if (bind(server_sock_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Bind failed on port " << port_ << "\n";
            return false;
        }
        
        if (listen(server_sock_, 5) < 0) {
            std::cerr << "Listen failed\n";
            return false;
        }
        
        running_ = true;
        std::cout << "Test GUI Server running at http://localhost:" << port_ << "\n";
        std::cout << "Open this URL in your browser to use the test interface.\n";
        std::cout << "Press Ctrl+C to stop.\n\n";
        
#ifdef _WIN32
        std::string url = "http://localhost:" + std::to_string(port_);
        ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
        
        while (running_) {
            SOCKET client = accept(server_sock_, nullptr, nullptr);
            if (client != INVALID_SOCKET) {
                std::thread(&TestGuiServer::handle_client, this, client).detach();
            }
        }
        
        return true;
    }
    
    void stop() {
        running_ = false;
        closesocket(server_sock_);
    }
    
private:
    int port_;
    std::string exe_dir_;
    std::string session_token_;
    SOCKET server_sock_;
    bool running_;
    FILE* test_process_;
    
    // PhoenixNest server process
#ifdef _WIN32
    HANDLE pn_server_process_ = NULL;
    DWORD pn_server_pid_ = 0;
#else
    pid_t pn_server_pid_ = 0;
#endif
    bool pn_server_running_ = false;
    int pn_ctrl_port_ = 5100;
    int pn_data_port_ = 5101;
    
    // PhoenixNest connection state
    SOCKET pn_ctrl_sock_ = INVALID_SOCKET;
    SOCKET pn_data_sock_ = INVALID_SOCKET;
    std::string pn_host_ = "127.0.0.1";
    bool pn_connected_ = false;
    
    // Brain modem connection state
    SOCKET brain_ctrl_sock_ = INVALID_SOCKET;
    SOCKET brain_data_sock_ = INVALID_SOCKET;
    std::string brain_host_ = "127.0.0.1";
    int brain_ctrl_port_ = 3999;
    int brain_data_port_ = 3998;
    bool brain_connected_ = false;
    
    std::string url_decode(const std::string& val) {
        std::string decoded;
        for (size_t i = 0; i < val.size(); i++) {
            if (val[i] == '%' && i + 2 < val.size()) {
                int hex;
                sscanf(val.substr(i + 1, 2).c_str(), "%x", &hex);
                decoded += (char)hex;
                i += 2;
            } else if (val[i] == '+') {
                decoded += ' ';
            } else {
                decoded += val[i];
            }
        }
        return decoded;
    }
    
    void handle_client(SOCKET client) {
        char header_buf[8192];
        int n = recv(client, header_buf, sizeof(header_buf) - 1, 0);
        if (n <= 0) {
            closesocket(client);
            return;
        }
        header_buf[n] = '\0';
        
        std::string request(header_buf, n);
        
        std::string method, path;
        std::istringstream iss(request);
        iss >> method >> path;
        
        if (path == "/" || path == "/index.html") {
            send_html(client, HTML_PAGE);
        } else if (path.find("/pn-server-start?") == 0) {
            handle_pn_server_start(client, path);
        } else if (path == "/pn-server-stop") {
            handle_pn_server_stop(client);
        } else if (path == "/pn-server-status") {
            handle_pn_server_status(client);
        } else if (path.find("/brain-connect?") == 0) {
            handle_brain_connect(client, path);
        } else if (path == "/brain-disconnect") {
            handle_brain_disconnect(client);
        } else if (path.find("/brain-to-pn-test?") == 0) {
            handle_brain_to_pn_test(client, path);
        } else if (path.find("/pn-to-brain-test?") == 0) {
            handle_pn_to_brain_test(client, path);
        } else if (path.find("/brain-to-pn-quick?") == 0) {
            handle_brain_to_pn_quick(client, path);
        } else if (path.find("/pn-to-brain-quick?") == 0) {
            handle_pn_to_brain_quick(client, path);
        } else {
            send_404(client);
        }
        
        closesocket(client);
    }
    
    void send_html(SOCKET client, const char* html) {
        std::string html_str(html);
        std::string token_script = "<script>window.SESSION_TOKEN='" + session_token_ + "';</script>";
        size_t head_end = html_str.find("</head>");
        if (head_end != std::string::npos) {
            html_str.insert(head_end, token_script);
        }
        
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: text/html; charset=utf-8\r\n"
                 << "Content-Length: " << html_str.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << html_str;
        std::string resp = response.str();
        send(client, resp.c_str(), (int)resp.size(), 0);
    }
    
    void send_404(SOCKET client) {
        const char* html = "<html><body><h1>404 Not Found</h1></body></html>";
        std::ostringstream response;
        response << "HTTP/1.1 404 Not Found\r\n"
                 << "Content-Type: text/html; charset=utf-8\r\n"
                 << "Content-Length: " << strlen(html) << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << html;
        std::string resp = response.str();
        send(client, resp.c_str(), (int)resp.size(), 0);
    }
    
    void send_json(SOCKET client, const std::string& json) {
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << json.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << json;
        std::string resp = response.str();
        send(client, resp.c_str(), (int)resp.size(), 0);
    }
    
    // ============ PHOENIXNEST SERVER CONTROL ============
    
    void handle_pn_server_start(SOCKET client, const std::string& path) {
        if (pn_server_running_ && pn_server_pid_ != 0) {
#ifdef _WIN32
            DWORD exitCode;
            if (GetExitCodeProcess(pn_server_process_, &exitCode) && exitCode == STILL_ACTIVE) {
                std::ostringstream json;
                json << "{\"success\":true,\"pid\":" << pn_server_pid_ << ",\"message\":\"Already running\"}";
                send_json(client, json.str());
                return;
            } else {
                CloseHandle(pn_server_process_);
                pn_server_process_ = NULL;
                pn_server_pid_ = 0;
                pn_server_running_ = false;
            }
#endif
        }
        
        int ctrl_port = 5100;
        int data_port = 5101;
        
        size_t pos = path.find("ctrl=");
        if (pos != std::string::npos) {
            ctrl_port = std::stoi(path.substr(pos + 5));
        }
        pos = path.find("data=");
        if (pos != std::string::npos) {
            data_port = std::stoi(path.substr(pos + 5));
        }
        
        pn_ctrl_port_ = ctrl_port;
        pn_data_port_ = data_port;
        
        std::vector<std::string> server_paths = {
            exe_dir_ + PATH_SEP + "m110a_server.exe",
            exe_dir_ + PATH_SEP + ".." + PATH_SEP + "server" + PATH_SEP + "m110a_server.exe",
        };
        
        std::string server_exe;
        for (const auto& p : server_paths) {
            if (fs::exists(p)) {
                server_exe = fs::absolute(p).string();
                break;
            }
        }
        
        if (server_exe.empty()) {
            send_json(client, "{\"success\":false,\"message\":\"m110a_server.exe not found\"}");
            return;
        }
        
#ifdef _WIN32
        std::string cmd = "\"" + server_exe + "\" --control-port " + std::to_string(ctrl_port) + 
                          " --data-port " + std::to_string(data_port);
        
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        char cmdLine[1024];
        strncpy(cmdLine, cmd.c_str(), sizeof(cmdLine) - 1);
        cmdLine[sizeof(cmdLine) - 1] = '\0';
        
        if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 
                          CREATE_NEW_CONSOLE | CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            pn_server_process_ = pi.hProcess;
            pn_server_pid_ = pi.dwProcessId;
            pn_server_running_ = true;
            CloseHandle(pi.hThread);
            
            Sleep(500);
            
            std::ostringstream json;
            json << "{\"success\":true,\"pid\":" << pn_server_pid_ << "}";
            send_json(client, json.str());
        } else {
            DWORD error = GetLastError();
            std::ostringstream json;
            json << "{\"success\":false,\"message\":\"CreateProcess failed: " << error << "\"}";
            send_json(client, json.str());
        }
#else
        send_json(client, "{\"success\":false,\"message\":\"Not implemented on this platform\"}");
#endif
    }
    
    void handle_pn_server_stop(SOCKET client) {
        if (!pn_server_running_ || pn_server_pid_ == 0) {
            pn_server_running_ = false;
            pn_server_pid_ = 0;
            send_json(client, "{\"success\":true,\"message\":\"Not running\"}");
            return;
        }
        
#ifdef _WIN32
        if (pn_server_process_ != NULL) {
            TerminateProcess(pn_server_process_, 0);
            WaitForSingleObject(pn_server_process_, 3000);
            CloseHandle(pn_server_process_);
            pn_server_process_ = NULL;
        }
        
        pn_server_pid_ = 0;
        pn_server_running_ = false;
        pn_disconnect();
        send_json(client, "{\"success\":true}");
#else
        send_json(client, "{\"success\":false,\"message\":\"Not implemented\"}");
#endif
    }
    
    void handle_pn_server_status(SOCKET client) {
        bool running = false;
        
#ifdef _WIN32
        if (pn_server_running_ && pn_server_process_ != NULL) {
            DWORD exitCode;
            if (GetExitCodeProcess(pn_server_process_, &exitCode) && exitCode == STILL_ACTIVE) {
                running = true;
            } else {
                CloseHandle(pn_server_process_);
                pn_server_process_ = NULL;
                pn_server_pid_ = 0;
                pn_server_running_ = false;
            }
        }
#endif
        
        std::ostringstream json;
        json << "{\"running\":" << (running ? "true" : "false");
        if (running) {
            json << ",\"pid\":" << pn_server_pid_ 
                 << ",\"ctrlPort\":" << pn_ctrl_port_
                 << ",\"dataPort\":" << pn_data_port_;
        }
        json << "}";
        send_json(client, json.str());
    }
    
    // ============ PHOENIXNEST CONNECTION ============
    
    bool pn_connect() {
        if (pn_connected_) return true;
        
        pn_ctrl_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (pn_ctrl_sock_ == INVALID_SOCKET) return false;
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(pn_ctrl_port_);
        inet_pton(AF_INET, pn_host_.c_str(), &addr.sin_addr);
        
        DWORD timeout = 5000;
        setsockopt(pn_ctrl_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(pn_ctrl_sock_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
        
        if (connect(pn_ctrl_sock_, (sockaddr*)&addr, sizeof(addr)) != 0) {
            closesocket(pn_ctrl_sock_);
            pn_ctrl_sock_ = INVALID_SOCKET;
            return false;
        }
        
        char buf[1024];
        int n = recv(pn_ctrl_sock_, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            closesocket(pn_ctrl_sock_);
            pn_ctrl_sock_ = INVALID_SOCKET;
            return false;
        }
        buf[n] = '\0';
        std::cout << "[PN] Control connected: " << buf << std::flush;
        
        pn_data_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (pn_data_sock_ == INVALID_SOCKET) {
            closesocket(pn_ctrl_sock_);
            pn_ctrl_sock_ = INVALID_SOCKET;
            return false;
        }
        
        addr.sin_port = htons(pn_data_port_);
        setsockopt(pn_data_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(pn_data_sock_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
        
        if (connect(pn_data_sock_, (sockaddr*)&addr, sizeof(addr)) != 0) {
            closesocket(pn_ctrl_sock_);
            closesocket(pn_data_sock_);
            pn_ctrl_sock_ = INVALID_SOCKET;
            pn_data_sock_ = INVALID_SOCKET;
            return false;
        }
        
        std::cout << "[PN] Data port connected\n";
        pn_connected_ = true;
        return true;
    }
    
    void pn_disconnect() {
        if (pn_ctrl_sock_ != INVALID_SOCKET) {
            closesocket(pn_ctrl_sock_);
            pn_ctrl_sock_ = INVALID_SOCKET;
        }
        if (pn_data_sock_ != INVALID_SOCKET) {
            closesocket(pn_data_sock_);
            pn_data_sock_ = INVALID_SOCKET;
        }
        pn_connected_ = false;
    }
    
    bool pn_send_cmd(const std::string& cmd) {
        if (pn_ctrl_sock_ == INVALID_SOCKET) return false;
        std::string msg = cmd + "\n";
        std::cout << "[PN] SEND: " << cmd << "\n";
        return send(pn_ctrl_sock_, msg.c_str(), (int)msg.size(), 0) > 0;
    }
    
    std::string pn_recv_ctrl(int timeout_ms = 5000) {
        if (pn_ctrl_sock_ == INVALID_SOCKET) return "";
        
        DWORD timeout = timeout_ms;
        setsockopt(pn_ctrl_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        
        char buf[4096];
        int n = recv(pn_ctrl_sock_, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            std::string result(buf);
            std::cout << "[PN] RECV: " << result.substr(0, 60) << "\n";
            return result;
        }
        return "";
    }
    
    std::vector<uint8_t> pn_recv_data(int timeout_ms = 10000) {
        std::vector<uint8_t> data;
        if (pn_data_sock_ == INVALID_SOCKET) return data;
        
        DWORD timeout = timeout_ms;
        setsockopt(pn_data_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        
        char buf[8192];
        while (true) {
            int n = recv(pn_data_sock_, buf, sizeof(buf), 0);
            if (n <= 0) break;
            data.insert(data.end(), buf, buf + n);
        }
        return data;
    }
    
    // ============ BRAIN MODEM CONNECTION ============
    
    void handle_brain_connect(SOCKET client, const std::string& path) {
        std::string host = "localhost";
        int ctrl_port = 3999;
        int data_port = 3998;
        
        size_t pos = path.find("host=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            host = url_decode(path.substr(pos + 5, end - pos - 5));
        }
        
        pos = path.find("ctrl=");
        if (pos != std::string::npos) {
            ctrl_port = std::stoi(path.substr(pos + 5));
        }
        
        pos = path.find("data=");
        if (pos != std::string::npos) {
            data_port = std::stoi(path.substr(pos + 5));
        }
        
        // Disconnect existing
        brain_disconnect();
        
        brain_host_ = host;
        brain_ctrl_port_ = ctrl_port;
        brain_data_port_ = data_port;
        
        // Connect
        struct addrinfo hints{}, *result;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        if (getaddrinfo(host.c_str(), std::to_string(ctrl_port).c_str(), &hints, &result) != 0) {
            send_json(client, "{\"success\":false,\"message\":\"Cannot resolve host\"}");
            return;
        }
        
        brain_ctrl_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (brain_ctrl_sock_ == INVALID_SOCKET) {
            freeaddrinfo(result);
            send_json(client, "{\"success\":false,\"message\":\"Socket creation failed\"}");
            return;
        }
        
        DWORD timeout = 5000;
        setsockopt(brain_ctrl_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(brain_ctrl_sock_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
        
        if (connect(brain_ctrl_sock_, result->ai_addr, (int)result->ai_addrlen) != 0) {
            closesocket(brain_ctrl_sock_);
            brain_ctrl_sock_ = INVALID_SOCKET;
            freeaddrinfo(result);
            send_json(client, "{\"success\":false,\"message\":\"Cannot connect to Brain control port\"}");
            return;
        }
        freeaddrinfo(result);
        
        // Wait for MODEM READY
        char buf[1024];
        int n = recv(brain_ctrl_sock_, buf, sizeof(buf) - 1, 0);
        std::string ready_msg = "Connected";
        if (n > 0) {
            buf[n] = '\0';
            ready_msg = buf;
            while (!ready_msg.empty() && (ready_msg.back() == '\n' || ready_msg.back() == '\r')) {
                ready_msg.pop_back();
            }
        }
        std::cout << "[BRAIN] Control connected: " << ready_msg << "\n";
        
        // Connect to data port
        if (getaddrinfo(host.c_str(), std::to_string(data_port).c_str(), &hints, &result) != 0) {
            closesocket(brain_ctrl_sock_);
            brain_ctrl_sock_ = INVALID_SOCKET;
            send_json(client, "{\"success\":false,\"message\":\"Cannot resolve data port\"}");
            return;
        }
        
        brain_data_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (brain_data_sock_ == INVALID_SOCKET) {
            closesocket(brain_ctrl_sock_);
            brain_ctrl_sock_ = INVALID_SOCKET;
            freeaddrinfo(result);
            send_json(client, "{\"success\":false,\"message\":\"Data socket creation failed\"}");
            return;
        }
        
        setsockopt(brain_data_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(brain_data_sock_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
        
        if (connect(brain_data_sock_, result->ai_addr, (int)result->ai_addrlen) != 0) {
            closesocket(brain_ctrl_sock_);
            closesocket(brain_data_sock_);
            brain_ctrl_sock_ = INVALID_SOCKET;
            brain_data_sock_ = INVALID_SOCKET;
            freeaddrinfo(result);
            send_json(client, "{\"success\":false,\"message\":\"Cannot connect to Brain data port\"}");
            return;
        }
        freeaddrinfo(result);
        
        std::cout << "[BRAIN] Data port connected\n";
        brain_connected_ = true;
        
        std::string escaped;
        for (char c : ready_msg) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c >= 32) escaped += c;
        }
        
        send_json(client, "{\"success\":true,\"message\":\"" + escaped + "\"}");
    }
    
    void handle_brain_disconnect(SOCKET client) {
        brain_disconnect();
        send_json(client, "{\"success\":true}");
    }
    
    void brain_disconnect() {
        if (brain_ctrl_sock_ != INVALID_SOCKET) {
            closesocket(brain_ctrl_sock_);
            brain_ctrl_sock_ = INVALID_SOCKET;
        }
        if (brain_data_sock_ != INVALID_SOCKET) {
            closesocket(brain_data_sock_);
            brain_data_sock_ = INVALID_SOCKET;
        }
        brain_connected_ = false;
    }
    
    bool brain_send_cmd(const std::string& cmd) {
        if (brain_ctrl_sock_ == INVALID_SOCKET) return false;
        std::string msg = cmd + "\n";
        std::cout << "[BRAIN] SEND: " << cmd << "\n";
        return send(brain_ctrl_sock_, msg.c_str(), (int)msg.size(), 0) > 0;
    }
    
    std::string brain_recv_ctrl(int timeout_ms = 5000) {
        if (brain_ctrl_sock_ == INVALID_SOCKET) return "";
        
        DWORD timeout = timeout_ms;
        setsockopt(brain_ctrl_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        
        char buf[4096];
        int n = recv(brain_ctrl_sock_, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            std::string result(buf);
            std::cout << "[BRAIN] RECV: " << result.substr(0, 60) << "\n";
            return result;
        }
        return "";
    }
    
    std::vector<uint8_t> brain_recv_data(int timeout_ms = 10000) {
        std::vector<uint8_t> data;
        if (brain_data_sock_ == INVALID_SOCKET) return data;
        
        DWORD timeout = timeout_ms;
        setsockopt(brain_data_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        
        char buf[8192];
        while (true) {
            int n = recv(brain_data_sock_, buf, sizeof(buf), 0);
            if (n <= 0) break;
            data.insert(data.end(), buf, buf + n);
        }
        return data;
    }
    
    // ============ CROSS-MODEM TESTS ============
    
    void handle_brain_to_pn_test(SOCKET client, const std::string& path) {
        // Brain TX â†’ PhoenixNest RX (streaming SSE)
        std::string mode = "600S", message = "TEST";
        
        size_t pos = path.find("mode=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            mode = url_decode(path.substr(pos + 5, end - pos - 5));
        }
        
        pos = path.find("message=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            message = url_decode(path.substr(pos + 8, end - pos - 8));
        }
        
        // Send SSE headers
        std::string headers = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "\r\n";
        send(client, headers.c_str(), (int)headers.size(), 0);
        
        auto send_sse = [&](const std::string& json) {
            std::string msg = "data: " + json + "\n\n";
            send(client, msg.c_str(), (int)msg.size(), 0);
        };
        
        // Check prerequisites
        if (!brain_connected_) {
            send_sse("{\"step\":0,\"status\":\"error\",\"result\":\"Brain modem not connected\",\"success\":false}");
            return;
        }
        
        if (!pn_server_running_) {
            send_sse("{\"step\":0,\"status\":\"error\",\"result\":\"PhoenixNest server not running\",\"success\":false}");
            return;
        }
        
        // Step 0: Set Brain data rate
        send_sse("{\"step\":0,\"status\":\"running\",\"log\":\"Setting Brain data rate: " + mode + "\",\"logType\":\"tx\"}");
        brain_send_cmd("CMD:DATA RATE:" + mode);
        std::string resp = brain_recv_ctrl(2000);
        if (resp.find("OK:DATA RATE") == std::string::npos) {
            send_sse("{\"step\":0,\"status\":\"error\",\"result\":\"Brain data rate not set: " + resp.substr(0, 30) + "\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":0,\"status\":\"complete\"}");
        
        // Step 1: Enable TX recording
        send_sse("{\"step\":1,\"status\":\"running\",\"log\":\"Enabling Brain TX recording\",\"logType\":\"tx\"}");
        brain_send_cmd("CMD:RECORD TX:ON");
        brain_recv_ctrl(1000);
        send_sse("{\"step\":1,\"status\":\"complete\"}");
        
        // Step 2: Send test message
        send_sse("{\"step\":2,\"status\":\"running\",\"log\":\"Sending: " + message + "\",\"logType\":\"tx\"}");
        if (brain_data_sock_ != INVALID_SOCKET) {
            send(brain_data_sock_, message.c_str(), (int)message.size(), 0);
        }
        send_sse("{\"step\":2,\"status\":\"complete\"}");
        
        // Step 3: Trigger SENDBUFFER
        send_sse("{\"step\":3,\"status\":\"running\",\"log\":\"Triggering Brain SENDBUFFER\",\"logType\":\"tx\"}");
        brain_send_cmd("CMD:SENDBUFFER");
        send_sse("{\"step\":3,\"status\":\"complete\"}");
        
        // Step 4: Wait for TX:COMPLETE
        send_sse("{\"step\":4,\"status\":\"running\",\"log\":\"Waiting for Brain TX:COMPLETE...\",\"logType\":\"info\"}");
        bool tx_done = false;
        std::string pcm_path;
        for (int i = 0; i < 60; i++) {
            resp = brain_recv_ctrl(1000);
            if (resp.find("TX:COMPLETE") != std::string::npos) {
                tx_done = true;
                // Extract PCM path from "TX:PCM:..." line
                size_t pcm_pos = resp.find("TX:PCM:");
                if (pcm_pos != std::string::npos) {
                    pcm_path = resp.substr(pcm_pos + 7);
                    size_t end = pcm_path.find_first_of("\r\n");
                    if (end != std::string::npos) pcm_path = pcm_path.substr(0, end);
                }
                break;
            }
            if (resp.find("TX:TRUE") != std::string::npos) {
                send_sse("{\"log\":\"Brain TX in progress...\",\"logType\":\"info\"}");
            }
        }
        if (!tx_done) {
            send_sse("{\"step\":4,\"status\":\"error\",\"result\":\"Brain TX timeout\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":4,\"status\":\"complete\",\"log\":\"Brain TX complete\",\"logType\":\"rx\"}");
        
        // Step 5: Find PCM file
        send_sse("{\"step\":5,\"status\":\"running\",\"log\":\"Finding Brain TX PCM file\",\"logType\":\"info\"}");
        if (pcm_path.empty()) {
            // Look in default location
            std::string brain_tx_dir = "./tx_pcm_out";
            std::filesystem::file_time_type newest_time;
            try {
                for (const auto& entry : fs::directory_iterator(brain_tx_dir)) {
                    if (entry.path().extension() == ".pcm") {
                        auto ftime = fs::last_write_time(entry);
                        if (pcm_path.empty() || ftime > newest_time) {
                            pcm_path = entry.path().string();
                            newest_time = ftime;
                        }
                    }
                }
            } catch (...) {}
        }
        
        if (pcm_path.empty() || !fs::exists(pcm_path)) {
            send_sse("{\"step\":5,\"status\":\"error\",\"result\":\"Brain TX PCM file not found\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":5,\"status\":\"complete\",\"log\":\"Found: " + pcm_path + "\",\"logType\":\"rx\"}");
        
        // Step 6: Connect to PhoenixNest if needed
        send_sse("{\"step\":6,\"status\":\"running\",\"log\":\"Connecting to PhoenixNest\",\"logType\":\"info\"}");
        if (!pn_connected_) {
            if (!pn_connect()) {
                send_sse("{\"step\":6,\"status\":\"error\",\"result\":\"Failed to connect to PhoenixNest\",\"success\":false}");
                return;
            }
        }
        send_sse("{\"step\":6,\"status\":\"complete\"}");
        
        // Step 7: Inject PCM into PhoenixNest RX
        send_sse("{\"step\":7,\"status\":\"running\",\"log\":\"Injecting PCM into PhoenixNest RX\",\"logType\":\"tx\"}");
        
        // Set PhoenixNest mode first
        pn_send_cmd("CMD:DATA RATE:" + mode);
        pn_recv_ctrl(2000);
        
        fs::path abs_pcm = fs::absolute(pcm_path);
        pn_send_cmd("CMD:RXAUDIOINJECT:" + abs_pcm.string());
        pn_recv_ctrl(2000);
        send_sse("{\"step\":7,\"status\":\"complete\"}");
        
        // Step 8: Wait for DCD
        send_sse("{\"step\":8,\"status\":\"running\",\"log\":\"Waiting for PhoenixNest DCD...\",\"logType\":\"info\"}");
        bool got_dcd = false;
        std::string detected_mode;
        for (int i = 0; i < 30; i++) {
            resp = pn_recv_ctrl(1000);
            if (resp.find("STATUS:RX:") != std::string::npos && resp.find("NO DCD") == std::string::npos) {
                got_dcd = true;
                size_t mpos = resp.find("STATUS:RX:");
                if (mpos != std::string::npos) {
                    detected_mode = resp.substr(mpos + 10);
                    size_t end = detected_mode.find_first_of("\r\n");
                    if (end != std::string::npos) detected_mode = detected_mode.substr(0, end);
                }
                break;
            }
            if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                break;
            }
        }
        if (!got_dcd) {
            send_sse("{\"step\":8,\"status\":\"error\",\"result\":\"No DCD from PhoenixNest\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":8,\"status\":\"complete\",\"log\":\"DCD: " + detected_mode + "\",\"logType\":\"rx\"}");
        
        // Step 9: Read decoded data
        send_sse("{\"step\":9,\"status\":\"running\",\"log\":\"Reading PhoenixNest decoded data...\",\"logType\":\"info\"}");
        auto decoded = pn_recv_data(5000);
        std::string decoded_str(decoded.begin(), decoded.end());
        send_sse("{\"step\":9,\"status\":\"complete\",\"log\":\"Received " + std::to_string(decoded.size()) + " bytes\",\"logType\":\"rx\"}");
        
        // Wait for inject complete
        for (int i = 0; i < 30; i++) {
            resp = pn_recv_ctrl(1000);
            if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos || resp.find("NO DCD") != std::string::npos) {
                break;
            }
        }
        
        // Step 10: Compare
        bool match = (decoded_str.find(message) != std::string::npos);
        if (match) {
            send_sse("{\"step\":9,\"status\":\"complete\",\"result\":\"SUCCESS: Decoded '" + decoded_str.substr(0, 40) + "' matches!\",\"success\":true}");
        } else {
            send_sse("{\"step\":9,\"status\":\"error\",\"result\":\"MISMATCH: Expected '" + message + "', got '" + decoded_str.substr(0, 40) + "'\",\"success\":false}");
        }
    }
    
    void handle_pn_to_brain_test(SOCKET client, const std::string& path) {
        // PhoenixNest TX â†’ Brain RX (streaming SSE)
        std::string mode = "600S", message = "TEST";
        
        size_t pos = path.find("mode=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            mode = url_decode(path.substr(pos + 5, end - pos - 5));
        }
        
        pos = path.find("message=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            message = url_decode(path.substr(pos + 8, end - pos - 8));
        }
        
        std::string headers = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "\r\n";
        send(client, headers.c_str(), (int)headers.size(), 0);
        
        auto send_sse = [&](const std::string& json) {
            std::string msg = "data: " + json + "\n\n";
            send(client, msg.c_str(), (int)msg.size(), 0);
        };
        
        // Check prerequisites
        if (!pn_server_running_) {
            send_sse("{\"step\":0,\"status\":\"error\",\"result\":\"PhoenixNest server not running\",\"success\":false}");
            return;
        }
        
        if (!brain_connected_) {
            send_sse("{\"step\":0,\"status\":\"error\",\"result\":\"Brain modem not connected\",\"success\":false}");
            return;
        }
        
        // Connect to PhoenixNest if needed
        if (!pn_connected_) {
            if (!pn_connect()) {
                send_sse("{\"step\":0,\"status\":\"error\",\"result\":\"Failed to connect to PhoenixNest\",\"success\":false}");
                return;
            }
        }
        
        // Step 0: Set PhoenixNest data rate
        send_sse("{\"step\":0,\"status\":\"running\",\"log\":\"Setting PhoenixNest data rate: " + mode + "\",\"logType\":\"tx\"}");
        pn_send_cmd("CMD:DATA RATE:" + mode);
        std::string resp = pn_recv_ctrl(2000);
        if (resp.find("OK:DATA RATE") == std::string::npos) {
            send_sse("{\"step\":0,\"status\":\"error\",\"result\":\"PhoenixNest data rate not set\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":0,\"status\":\"complete\"}");
        
        // Step 1: Enable TX recording
        send_sse("{\"step\":1,\"status\":\"running\",\"log\":\"Enabling PhoenixNest TX recording\",\"logType\":\"tx\"}");
        pn_send_cmd("CMD:RECORD TX:ON");
        pn_recv_ctrl(1000);
        send_sse("{\"step\":1,\"status\":\"complete\"}");
        
        // Step 2: Send test message
        send_sse("{\"step\":2,\"status\":\"running\",\"log\":\"Sending: " + message + "\",\"logType\":\"tx\"}");
        if (pn_data_sock_ != INVALID_SOCKET) {
            send(pn_data_sock_, message.c_str(), (int)message.size(), 0);
        }
        send_sse("{\"step\":2,\"status\":\"complete\"}");
        
        // Step 3: Trigger SENDBUFFER
        send_sse("{\"step\":3,\"status\":\"running\",\"log\":\"Triggering PhoenixNest SENDBUFFER\",\"logType\":\"tx\"}");
        pn_send_cmd("CMD:SENDBUFFER");
        send_sse("{\"step\":3,\"status\":\"complete\"}");
        
        // Step 4: Wait for TX:IDLE
        send_sse("{\"step\":4,\"status\":\"running\",\"log\":\"Waiting for PhoenixNest TX:IDLE...\",\"logType\":\"info\"}");
        bool tx_done = false;
        for (int i = 0; i < 60; i++) {
            resp = pn_recv_ctrl(1000);
            if (resp.find("STATUS:TX:IDLE") != std::string::npos) {
                tx_done = true;
                break;
            }
        }
        if (!tx_done) {
            send_sse("{\"step\":4,\"status\":\"error\",\"result\":\"PhoenixNest TX timeout\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":4,\"status\":\"complete\",\"log\":\"PhoenixNest TX complete\",\"logType\":\"rx\"}");
        
        // Step 5: Get PCM file path from SENDBUFFER response
        send_sse("{\"step\":5,\"status\":\"running\",\"log\":\"Getting PhoenixNest TX PCM file\",\"logType\":\"info\"}");
        std::string sendbuffer_resp = pn_recv_ctrl(2000);
        std::string pcm_path;
        size_t file_pos = sendbuffer_resp.find("FILE:");
        if (file_pos != std::string::npos) {
            pcm_path = sendbuffer_resp.substr(file_pos + 5);
            size_t end = pcm_path.find_first_of("\r\n");
            if (end != std::string::npos) pcm_path = pcm_path.substr(0, end);
        }
        
        if (pcm_path.empty() || !fs::exists(pcm_path)) {
            send_sse("{\"step\":5,\"status\":\"error\",\"result\":\"PhoenixNest TX PCM file not found\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":5,\"status\":\"complete\",\"log\":\"Found: " + pcm_path + "\",\"logType\":\"rx\"}");
        
        // Step 6: Inject PCM into Brain RX
        send_sse("{\"step\":6,\"status\":\"running\",\"log\":\"Injecting PCM into Brain RX\",\"logType\":\"tx\"}");
        
        // Set Brain mode first
        brain_send_cmd("CMD:DATA RATE:" + mode);
        brain_recv_ctrl(2000);
        
        fs::path abs_pcm = fs::absolute(pcm_path);
        brain_send_cmd("CMD:RXAUDIOINJECT:" + abs_pcm.string());
        brain_recv_ctrl(2000);
        send_sse("{\"step\":6,\"status\":\"complete\"}");
        
        // Step 7: Wait for Brain DCD
        send_sse("{\"step\":7,\"status\":\"running\",\"log\":\"Waiting for Brain DCD...\",\"logType\":\"info\"}");
        bool got_dcd = false;
        std::string detected_mode;
        for (int i = 0; i < 30; i++) {
            resp = brain_recv_ctrl(1000);
            if (resp.find("DCD:TRUE") != std::string::npos) {
                got_dcd = true;
                break;
            }
            if (resp.find("RX:COMPLETE") != std::string::npos) {
                got_dcd = true;  // Injection finished, check results
                break;
            }
        }
        if (!got_dcd) {
            send_sse("{\"step\":7,\"status\":\"error\",\"result\":\"No DCD from Brain\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":7,\"status\":\"complete\",\"log\":\"Brain DCD detected\",\"logType\":\"rx\"}");
        
        // Step 8: Read decoded data
        send_sse("{\"step\":8,\"status\":\"running\",\"log\":\"Reading Brain decoded data...\",\"logType\":\"info\"}");
        auto decoded = brain_recv_data(5000);
        std::string decoded_str(decoded.begin(), decoded.end());
        send_sse("{\"step\":8,\"status\":\"complete\",\"log\":\"Received " + std::to_string(decoded.size()) + " bytes\",\"logType\":\"rx\"}");
        
        // Wait for RX complete
        for (int i = 0; i < 30; i++) {
            resp = brain_recv_ctrl(1000);
            if (resp.find("RX:COMPLETE") != std::string::npos || resp.find("DCD:FALSE") != std::string::npos) {
                break;
            }
        }
        
        // Step 9: Compare
        bool match = (decoded_str.find(message) != std::string::npos);
        if (match) {
            send_sse("{\"step\":9,\"status\":\"complete\",\"result\":\"SUCCESS: Decoded '" + decoded_str.substr(0, 40) + "' matches!\",\"success\":true}");
        } else {
            send_sse("{\"step\":9,\"status\":\"error\",\"result\":\"MISMATCH: Expected '" + message + "', got '" + decoded_str.substr(0, 40) + "'\",\"success\":false}");
        }
    }
    
    void handle_brain_to_pn_quick(SOCKET client, const std::string& path) {
        // Quick version for matrix testing
        std::string mode = "600S", message = "TEST";
        
        size_t pos = path.find("mode=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            mode = url_decode(path.substr(pos + 5, end - pos - 5));
        }
        
        pos = path.find("message=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            message = url_decode(path.substr(pos + 8, end - pos - 8));
        }
        
        if (!brain_connected_) {
            send_json(client, "{\"success\":false,\"error\":\"Brain modem not connected\"}");
            return;
        }
        
        if (!pn_server_running_) {
            send_json(client, "{\"success\":false,\"error\":\"PhoenixNest server not running\"}");
            return;
        }
        
        // Brain TX
        brain_send_cmd("CMD:DATA RATE:" + mode);
        std::string resp = brain_recv_ctrl(2000);
        
        brain_send_cmd("CMD:RECORD TX:ON");
        brain_recv_ctrl(1000);
        
        if (brain_data_sock_ != INVALID_SOCKET) {
            send(brain_data_sock_, message.c_str(), (int)message.size(), 0);
        }
        
        brain_send_cmd("CMD:SENDBUFFER");
        
        // Wait for TX complete
        bool tx_done = false;
        std::string pcm_path;
        for (int i = 0; i < 60; i++) {
            resp = brain_recv_ctrl(1000);
            if (resp.find("TX:COMPLETE") != std::string::npos) {
                tx_done = true;
                size_t pcm_pos = resp.find("TX:PCM:");
                if (pcm_pos != std::string::npos) {
                    pcm_path = resp.substr(pcm_pos + 7);
                    size_t end = pcm_path.find_first_of("\r\n");
                    if (end != std::string::npos) pcm_path = pcm_path.substr(0, end);
                }
                break;
            }
        }
        
        if (!tx_done) {
            send_json(client, "{\"success\":false,\"error\":\"Brain TX timeout\"}");
            return;
        }
        
        // Find PCM if not in response
        if (pcm_path.empty()) {
            std::string brain_tx_dir = "./tx_pcm_out";
            std::filesystem::file_time_type newest_time;
            try {
                for (const auto& entry : fs::directory_iterator(brain_tx_dir)) {
                    if (entry.path().extension() == ".pcm") {
                        auto ftime = fs::last_write_time(entry);
                        if (pcm_path.empty() || ftime > newest_time) {
                            pcm_path = entry.path().string();
                            newest_time = ftime;
                        }
                    }
                }
            } catch (...) {}
        }
        
        if (pcm_path.empty()) {
            send_json(client, "{\"success\":false,\"error\":\"Brain TX PCM not found\"}");
            return;
        }
        
        // Connect to PN if needed
        if (!pn_connected_) {
            if (!pn_connect()) {
                send_json(client, "{\"success\":false,\"error\":\"Cannot connect to PhoenixNest\"}");
                return;
            }
        }
        
        // Set PN mode and inject
        pn_send_cmd("CMD:DATA RATE:" + mode);
        pn_recv_ctrl(2000);
        
        fs::path abs_pcm = fs::absolute(pcm_path);
        pn_send_cmd("CMD:RXAUDIOINJECT:" + abs_pcm.string());
        pn_recv_ctrl(2000);
        
        // Wait for DCD
        bool got_dcd = false;
        for (int i = 0; i < 30; i++) {
            resp = pn_recv_ctrl(1000);
            if (resp.find("STATUS:RX:") != std::string::npos && resp.find("NO DCD") == std::string::npos) {
                got_dcd = true;
            }
            if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                break;
            }
        }
        
        if (!got_dcd) {
            send_json(client, "{\"success\":false,\"error\":\"No DCD from PhoenixNest\"}");
            return;
        }
        
        // Read decoded
        auto decoded = pn_recv_data(5000);
        std::string decoded_str(decoded.begin(), decoded.end());
        
        // Wait for complete
        for (int i = 0; i < 30; i++) {
            resp = pn_recv_ctrl(1000);
            if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos || resp.find("NO DCD") != std::string::npos) {
                break;
            }
        }
        
        bool match = (decoded_str.find(message) != std::string::npos);
        
        std::ostringstream json;
        json << "{\"success\":" << (match ? "true" : "false")
             << ",\"decoded\":" << decoded.size()
             << ",\"error\":\"" << (match ? "" : "Message mismatch") << "\"}";
        send_json(client, json.str());
    }
    
    void handle_pn_to_brain_quick(SOCKET client, const std::string& path) {
        // Quick version for matrix testing
        std::string mode = "600S", message = "TEST";
        
        size_t pos = path.find("mode=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            mode = url_decode(path.substr(pos + 5, end - pos - 5));
        }
        
        pos = path.find("message=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            message = url_decode(path.substr(pos + 8, end - pos - 8));
        }
        
        if (!pn_server_running_) {
            send_json(client, "{\"success\":false,\"error\":\"PhoenixNest server not running\"}");
            return;
        }
        
        if (!brain_connected_) {
            send_json(client, "{\"success\":false,\"error\":\"Brain modem not connected\"}");
            return;
        }
        
        // Connect to PN if needed
        if (!pn_connected_) {
            if (!pn_connect()) {
                send_json(client, "{\"success\":false,\"error\":\"Cannot connect to PhoenixNest\"}");
                return;
            }
        }
        
        // PN TX
        pn_send_cmd("CMD:DATA RATE:" + mode);
        std::string resp = pn_recv_ctrl(2000);
        
        pn_send_cmd("CMD:RECORD TX:ON");
        pn_recv_ctrl(1000);
        
        if (pn_data_sock_ != INVALID_SOCKET) {
            send(pn_data_sock_, message.c_str(), (int)message.size(), 0);
        }
        
        pn_send_cmd("CMD:SENDBUFFER");
        
        // Wait for TX:IDLE
        bool tx_done = false;
        for (int i = 0; i < 60; i++) {
            resp = pn_recv_ctrl(1000);
            if (resp.find("STATUS:TX:IDLE") != std::string::npos) {
                tx_done = true;
                break;
            }
        }
        
        if (!tx_done) {
            send_json(client, "{\"success\":false,\"error\":\"PhoenixNest TX timeout\"}");
            return;
        }
        
        // Get PCM path
        std::string sendbuffer_resp = pn_recv_ctrl(2000);
        std::string pcm_path;
        size_t file_pos = sendbuffer_resp.find("FILE:");
        if (file_pos != std::string::npos) {
            pcm_path = sendbuffer_resp.substr(file_pos + 5);
            size_t end = pcm_path.find_first_of("\r\n");
            if (end != std::string::npos) pcm_path = pcm_path.substr(0, end);
        }
        
        if (pcm_path.empty()) {
            send_json(client, "{\"success\":false,\"error\":\"PhoenixNest TX PCM not found\"}");
            return;
        }
        
        // Set Brain mode and inject
        brain_send_cmd("CMD:DATA RATE:" + mode);
        brain_recv_ctrl(2000);
        
        fs::path abs_pcm = fs::absolute(pcm_path);
        brain_send_cmd("CMD:RXAUDIOINJECT:" + abs_pcm.string());
        brain_recv_ctrl(2000);
        
        // Wait for DCD / RX complete
        bool got_dcd = false;
        for (int i = 0; i < 30; i++) {
            resp = brain_recv_ctrl(1000);
            if (resp.find("DCD:TRUE") != std::string::npos) {
                got_dcd = true;
            }
            if (resp.find("RX:COMPLETE") != std::string::npos) {
                got_dcd = true;
                break;
            }
        }
        
        if (!got_dcd) {
            send_json(client, "{\"success\":false,\"error\":\"No DCD from Brain\"}");
            return;
        }
        
        // Read decoded
        auto decoded = brain_recv_data(5000);
        std::string decoded_str(decoded.begin(), decoded.end());
        
        // Wait for complete
        for (int i = 0; i < 30; i++) {
            resp = brain_recv_ctrl(1000);
            if (resp.find("RX:COMPLETE") != std::string::npos || resp.find("DCD:FALSE") != std::string::npos) {
                break;
            }
        }
        
        bool match = (decoded_str.find(message) != std::string::npos);
        
        std::ostringstream json;
        json << "{\"success\":" << (match ? "true" : "false")
             << ",\"decoded\":" << decoded.size()
             << ",\"error\":\"" << (match ? "" : "Message mismatch") << "\"}";
        send_json(client, json.str());
    }
};

int main(int argc, char* argv[]) {
    int port = 8080;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "M110A Test GUI Server - Cross-Modem Interop Edition\n\n";
            std::cout << "Usage: " << argv[0] << " [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --port N, -p N   HTTP port (default: 8080)\n";
            std::cout << "  --help, -h       Show this help\n\n";
            std::cout << "Features:\n";
            std::cout << "  - PhoenixNest server control\n";
            std::cout << "  - Brain modem connection\n";
            std::cout << "  - Cross-modem interop testing (Brain <-> PhoenixNest)\n";
            return 0;
        }
    }
    
    TestGuiServer server(port);
    server.start();
    
    return 0;
}
