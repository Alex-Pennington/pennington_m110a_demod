/**
 * @file test_gui_server.cpp
 * @brief Web-based GUI for M110A Exhaustive Test Suite
 * 
 * Provides a simple HTTP server that serves a web UI for running tests.
 * Launches the unified exhaustive_test.exe and streams output to browser.
 * 
 * Usage:
 *   test_gui.exe [--port N]
 *   Then open http://localhost:8080 in browser
 */

#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket close
#endif

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <regex>
#include <random>
#include <ctime>

#include "common/license.h"

namespace fs = std::filesystem;

// HTML page with embedded JavaScript
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
        
        /* Reports tab styles */
        .reports-container { background: #16213e; padding: 20px; border-radius: 0 8px 8px 8px; }
        .reports-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
        .reports-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); gap: 15px; }
        .report-card { background: #0f0f23; border: 1px solid #333; border-radius: 8px; padding: 15px;
                      cursor: pointer; transition: all 0.2s; }
        .report-card:hover { border-color: #00d4ff; transform: translateY(-2px); }
        .report-card.progressive { border-left: 4px solid #00d4ff; }
        .report-card.exhaustive { border-left: 4px solid #5fff5f; }
        .report-card .type { font-size: 10px; text-transform: uppercase; letter-spacing: 1px;
                            color: #00d4ff; margin-bottom: 5px; }
        .report-card.exhaustive .type { color: #5fff5f; }
        .report-card .title { font-size: 14px; font-weight: bold; color: #fff; margin-bottom: 10px; }
        .report-card .meta { display: grid; grid-template-columns: 1fr 1fr; gap: 5px; font-size: 11px; color: #888; }
        .report-card .meta-item { display: flex; align-items: center; gap: 5px; }
        .report-card .meta-item .label { color: #666; }
        .report-card .meta-item .value { color: #aaa; }
        .report-card .actions { display: flex; justify-content: flex-end; margin-top: 10px; gap: 8px; }
        .report-card .btn-card { padding: 5px 10px; border: none; border-radius: 4px; cursor: pointer; 
                                 font-size: 11px; transition: all 0.2s; }
        .report-card .btn-upload-card { background: #00d4ff; color: #000; }
        .report-card .btn-upload-card:hover { background: #00a8cc; }
        .report-card .btn-upload-card:disabled { background: #444; color: #888; cursor: not-allowed; }
        .popup { position: fixed; top: 20px; right: 20px; padding: 15px 25px; border-radius: 8px; 
                 z-index: 1000; animation: slideIn 0.3s ease; max-width: 400px; }
        .popup.success { background: #1e5f3a; color: #fff; border: 1px solid #2a8f52; }
        .popup.error { background: #5f1e1e; color: #fff; border: 1px solid #8f2a2a; }
        @keyframes slideIn { from { transform: translateX(100%); opacity: 0; } to { transform: translateX(0); opacity: 1; } }
        .popup a { color: #00d4ff; }
        .report-viewer { display: none; background: #0f0f23; border: 1px solid #333; border-radius: 8px;
                        padding: 20px; margin-top: 15px; max-height: 600px; overflow-y: auto; }
        .report-viewer.active { display: block; }
        .report-viewer h2 { color: #00d4ff; margin-top: 0; }
        .report-viewer pre { white-space: pre-wrap; font-size: 12px; }
        .report-viewer table { border-collapse: collapse; width: 100%; margin: 10px 0; }
        .report-viewer th, .report-viewer td { border: 1px solid #333; padding: 8px; text-align: left; }
        .report-viewer th { background: #16213e; color: #00d4ff; }
        .no-reports { text-align: center; padding: 40px; color: #666; }
        
        /* Support tab styles */
        .support-container { background: #16213e; padding: 20px; border-radius: 0 8px 8px 8px; }
        .support-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }
        .support-buttons { display: flex; gap: 10px; flex-wrap: wrap; }
        .btn-support { padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-weight: bold; }
        .btn-upload { background: #00d4ff; color: #000; }
        .btn-upload:hover { background: #00a8cc; }
        .btn-upload:disabled { background: #444; color: #888; cursor: not-allowed; }
        .btn-bug { background: #ff4757; color: #fff; }
        .btn-feature { background: #5f5fff; color: #fff; }
        .btn-help { background: #5fff5f; color: #000; }
        .btn-docs { background: #ff9f43; color: #000; }
        .support-iframe { width: 100%; height: 700px; border: 1px solid #333; border-radius: 8px; background: #fff; }
        .upload-status { margin-top: 10px; padding: 10px; border-radius: 4px; display: none; }
        .upload-status.success { display: block; background: #1e5f3a; color: #fff; }
        .upload-status.error { display: block; background: #5f1e1e; color: #fff; }
        
        /* License tab styles */
        .license-container { background: #16213e; padding: 20px; border-radius: 0 8px 8px 8px; }
        .license-status { padding: 20px; border-radius: 8px; margin-bottom: 20px; }
        .license-status.valid { background: #1e5f3a; border: 1px solid #2a8f52; }
        .license-status.invalid { background: #5f1e1e; border: 1px solid #8f2a2a; }
        .license-status.pending { background: #5f5f1e; border: 1px solid #8f8f2a; }
        .license-status.checking { background: #1e3a5f; border: 1px solid #2a528f; }
        .license-status h3 { margin: 0 0 10px 0; color: #fff; }
        .license-status p { margin: 5px 0; color: #ccc; }
        .license-status .hwid { font-family: 'Consolas', monospace; color: #00d4ff; background: #0f0f23; 
                                padding: 5px 10px; border-radius: 4px; display: inline-block; margin-top: 5px; }
        .license-form { background: #0f0f23; padding: 20px; border-radius: 8px; margin-top: 20px; }
        .license-form h3 { color: #00d4ff; margin-top: 0; }
        .license-form .form-row { margin-bottom: 15px; }
        .license-form label { display: block; margin-bottom: 5px; color: #aaa; }
        .license-form input { width: 100%; max-width: 400px; padding: 10px; border: 1px solid #333; 
                              border-radius: 4px; background: #16213e; color: #fff; }
        .license-form input:focus { border-color: #00d4ff; outline: none; }
        .license-form .btn-row { margin-top: 20px; display: flex; gap: 10px; }
        .btn-license { padding: 12px 25px; border: none; border-radius: 4px; cursor: pointer; font-weight: bold; }
        .btn-request { background: #00d4ff; color: #000; }
        .btn-request:hover { background: #00a8cc; }
        .btn-request:disabled { background: #444; color: #888; cursor: not-allowed; }
        .btn-check { background: #5f5fff; color: #fff; }
        .btn-check:hover { background: #4a4acc; }
        .btn-validate { background: #5fff5f; color: #000; }
        .btn-validate:hover { background: #4acc4a; }
        .license-info { margin-top: 20px; padding: 15px; background: #0f3460; border-radius: 8px; }
        .license-info h4 { color: #00d4ff; margin: 0 0 10px 0; }
        .license-info table { width: 100%; }
        .license-info td { padding: 5px 10px; color: #ccc; }
        .license-info td:first-child { color: #888; width: 120px; }
        .license-key-display { font-family: 'Consolas', monospace; font-size: 12px; word-break: break-all;
                               background: #0f0f23; padding: 10px; border-radius: 4px; margin-top: 10px; color: #5fff5f; }
    </style>
</head>
<body>
    <div class="container">
        <h1>M110A Modem Test Suite</h1>
        
        <div class="tabs">
            <button class="tab active" onclick="showTab('tests')">Run Tests</button>
            <button class="tab" onclick="showTab('reports')">Reports</button>
            <button class="tab" onclick="showTab('license')">License</button>
            <button class="tab" onclick="showTab('support')">Support</button>
        </div>
        
        <div id="tab-tests" class="tab-content active">
        <div class="controls">
            <div class="row">
                <div class="field">
                    <label>Modes (Ctrl+click to multi-select)</label>
                    <select id="modes" multiple>
                        <option value="75S">75S</option>
                        <option value="75L">75L</option>
                        <option value="150S">150S</option>
                        <option value="150L">150L</option>
                        <option value="300S">300S</option>
                        <option value="300L">300L</option>
                        <option value="600S">600S</option>
                        <option value="600L">600L</option>
                        <option value="1200S">1200S</option>
                        <option value="1200L">1200L</option>
                        <option value="2400S">2400S</option>
                        <option value="2400L">2400L</option>
                    </select>
                    <div class="select-hint">Empty = All modes</div>
                </div>
                <div class="field">
                    <label>Equalizers (Ctrl+click to multi-select)</label>
                    <select id="equalizers" multiple>
                        <option value="DFE" selected>DFE</option>
                        <option value="NONE">None</option>
                        <option value="DFE_RLS">DFE RLS</option>
                        <option value="MLSE_L2">MLSE L=2</option>
                        <option value="MLSE_L3">MLSE L=3</option>
                        <option value="MLSE_ADAPTIVE">MLSE Adaptive</option>
                        <option value="TURBO">Turbo</option>
                    </select>
                    <div class="select-hint">Empty = DFE only</div>
                </div>
                <div class="field">
                    <label>Iterations</label>
                    <input type="number" id="iterations" value="1" min="1" max="100" style="width: 80px;">
                </div>
                <div class="field">
                    <label>Parallel Threads</label>
                    <select id="threads">
                        <option value="1">1 (Sequential)</option>
                        <option value="2">2 threads</option>
                        <option value="4" selected>4 threads</option>
                        <option value="6">6 threads</option>
                        <option value="8">8 threads</option>
                        <option value="12">12 threads</option>
                        <option value="16">16 threads</option>
                    </select>
                </div>
                <div class="field">
                    <label>Backend</label>
                    <select id="backend">
                        <option value="direct">Direct API</option>
                        <option value="server">Server (TCP)</option>
                    </select>
                </div>
                <div class="field">
                    <label>Mode Detection</label>
                    <select id="detection">
                        <option value="known">Known Mode (Default)</option>
                        <option value="auto">Auto-Detect</option>
                    </select>
                </div>
            </div>
            
            <div class="row">
                <div class="field">
                    <label>Quick Select</label>
                    <div class="checkbox-group">
                        <button type="button" onclick="selectAllModes()" style="padding: 5px 10px;">All Modes</button>
                        <button type="button" onclick="selectShortModes()" style="padding: 5px 10px;">Short Only</button>
                        <button type="button" onclick="selectLongModes()" style="padding: 5px 10px;">Long Only</button>
                        <button type="button" onclick="clearModes()" style="padding: 5px 10px;">Clear</button>
                    </div>
                </div>
                <div class="field">
                    <label>&nbsp;</label>
                    <div class="checkbox-group">
                        <button type="button" onclick="selectAllEqualizers()" style="padding: 5px 10px;">All EQs</button>
                        <button type="button" onclick="clearEqualizers()" style="padding: 5px 10px;">Clear</button>
                    </div>
                </div>
            </div>
            
            <div class="test-summary" id="test-summary">Will run: <strong>All modes</strong> with <strong>DFE</strong> equalizer (12 tests)</div>
            
            <div class="row">
                <div class="field">
                    <label>Test Type</label>
                    <div class="checkbox-group">
                        <label><input type="radio" name="testtype" value="standard" checked> Standard</label>
                        <label><input type="radio" name="testtype" value="progressive"> Progressive</label>
                        <label><input type="radio" name="testtype" value="reference"> Reference Samples</label>
                    </div>
                </div>
            </div>
            
            <div class="row" id="prog-options" style="display: none;">
                <div class="field">
                    <label>Progressive Tests</label>
                    <div class="checkbox-group">
                        <label><input type="checkbox" id="prog-snr" checked> SNR</label>
                        <label><input type="checkbox" id="prog-freq" checked> Frequency</label>
                        <label><input type="checkbox" id="prog-multipath" checked> Multipath</label>
                    </div>
                </div>
                <div class="field">
                    <label>Output</label>
                    <div class="checkbox-group">
                        <label><input type="checkbox" id="csv-output"> Save CSV</label>
                        <input type="text" id="csv-filename" value="progressive_results.csv" style="width: 180px; margin-left: 10px;">
                    </div>
                </div>
            </div>
            
            <div class="row" id="ref-options" style="display: none;">
                <div class="field">
                    <label>Reference Sample Options</label>
                    <div class="checkbox-group" style="color: #aaa;">
                        Tests MS-DMT compatibility with all 12 reference samples
                    </div>
                </div>
            </div>
            
            <div class="row">
                <button class="btn-run" id="btn-run" onclick="runTest()">[Run Test]</button>
                <button class="btn-stop" id="btn-stop" onclick="stopTest()" disabled>[Stop]</button>
            </div>
            
            <div class="progress" id="progress-container" style="display: none;">
                <div class="progress-bar" id="progress-bar"></div>
            </div>
        </div>
        
        <div id="status" class="status status-idle">Ready</div>
        
        <div class="output" id="output">Welcome to M110A Modem Test Suite

Select options above and click "Run Test" to begin.

Available tests:
• Standard: Run through all channel conditions
• Progressive: Find performance limits (SNR, freq offset, multipath)
• Reference Samples: Test MS-DMT compatibility (14 reference samples)

</div>
        </div><!-- end tab-tests -->
        
        <div id="tab-reports" class="tab-content">
            <div class="reports-container">
                <div class="reports-header">
                    <h2 style="margin: 0; color: #00d4ff;">Test Reports</h2>
                    <button class="btn-refresh" onclick="loadReports()">Refresh</button>
                </div>
                <div id="reports-grid" class="reports-grid">
                    <div class="no-reports">Loading reports...</div>
                </div>
                <div id="report-viewer" class="report-viewer"></div>
            </div>
        </div><!-- end tab-reports -->
        
        <div id="tab-support" class="tab-content">
            <div class="support-container">
                <div class="support-header">
                    <h2 style="margin: 0; color: #00d4ff;">Support & Feedback</h2>
                    <div class="support-buttons">
                        <button class="btn-support btn-upload" id="btn-upload" onclick="uploadReport()">
                            📊 Upload Diagnostic Report
                        </button>
                        <button class="btn-support btn-bug" onclick="openSupport('bug')">
                            🐛 Report Bug
                        </button>
                        <button class="btn-support btn-feature" onclick="openSupport('feature')">
                            💡 Request Feature
                        </button>
                        <button class="btn-support btn-help" onclick="openSupport('question')">
                            ❓ Get Help
                        </button>
                        <button class="btn-support btn-docs" onclick="openSupport('docs')">
                            📚 Documentation
                        </button>
                    </div>
                </div>
                <div id="upload-status" class="upload-status"></div>
                <iframe id="support-iframe" class="support-iframe" src="https://www.organicengineer.com/software/issues"></iframe>
            </div>
        </div><!-- end tab-support -->
        
        <div id="tab-license" class="tab-content">
            <div class="license-container">
                <div id="license-status" class="license-status checking">
                    <h3>⏳ Checking License...</h3>
                    <p>Please wait while we verify your license status.</p>
                </div>
                
                <div id="license-info" class="license-info" style="display: none;">
                    <h4>📋 License Details</h4>
                    <table>
                        <tr><td>Customer:</td><td id="lic-customer">-</td></tr>
                        <tr><td>Status:</td><td id="lic-status">-</td></tr>
                        <tr><td>Expiry:</td><td id="lic-expiry">-</td></tr>
                        <tr><td>Hardware ID:</td><td id="lic-hwid">-</td></tr>
                    </table>
                    <div id="lic-key-display" class="license-key-display" style="display: none;"></div>
                </div>
                
                <div id="license-form" class="license-form" style="display: none;">
                    <h3>🔑 Request a License</h3>
                    <p style="color: #aaa; margin-bottom: 20px;">Fill out the form below to request a license key for this machine.</p>
                    
                    <div class="form-row">
                        <label for="lic-name">Full Name *</label>
                        <input type="text" id="lic-name" placeholder="John Doe" required>
                    </div>
                    
                    <div class="form-row">
                        <label for="lic-email">Email Address *</label>
                        <input type="email" id="lic-email" placeholder="john@example.com" required>
                    </div>
                    
                    <div class="form-row">
                        <label for="lic-company">Company/Organization (optional)</label>
                        <input type="text" id="lic-company" placeholder="Acme Corp">
                    </div>
                    
                    <div class="form-row">
                        <label for="lic-usecase">Use Case (optional)</label>
                        <input type="text" id="lic-usecase" placeholder="Amateur radio digital modes">
                    </div>
                    
                    <div class="btn-row">
                        <button class="btn-license btn-request" id="btn-request-license" onclick="requestLicense()">
                            📤 Submit License Request
                        </button>
                        <button class="btn-license btn-check" onclick="checkForLicense()">
                            🔄 Check for Pending License
                        </button>
                        <button class="btn-license btn-validate" onclick="validateLicense()">
                            ✓ Re-validate Current License
                        </button>
                    </div>
                </div>
                
                <div id="license-message" class="upload-status" style="margin-top: 15px;"></div>
            </div>
        </div><!-- end tab-license -->
    </div>
    
    <script>
        let eventSource = null;
        const ALL_MODES = ['75S','75L','150S','150L','300S','300L','600S','600L','1200S','1200L','2400S','2400L'];
        const ALL_EQS = ['DFE','NONE','DFE_RLS','MLSE_L2','MLSE_L3','MLSE_ADAPTIVE','TURBO'];
        
        // Tab switching
        function showTab(tabName) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            document.querySelector('.tab[onclick*="' + tabName + '"]').classList.add('active');
            document.getElementById('tab-' + tabName).classList.add('active');
            
            if (tabName === 'reports') {
                loadReports();
            }
            if (tabName === 'license') {
                checkLicenseStatus();
            }
        }
        
        // ============ LICENSE MANAGEMENT ============
        const LICENSE_API = 'https://www.organicengineer.com/software/api/license';
        let currentHwid = '';
        let currentLicenseKey = '';
        
        // Check license status on page load and when tab is selected
        async function checkLicenseStatus() {
            const statusDiv = document.getElementById('license-status');
            const infoDiv = document.getElementById('license-info');
            const formDiv = document.getElementById('license-form');
            const msgDiv = document.getElementById('license-message');
            
            statusDiv.className = 'license-status checking';
            statusDiv.innerHTML = '<h3>⏳ Checking License...</h3><p>Please wait...</p>';
            infoDiv.style.display = 'none';
            msgDiv.style.display = 'none';
            
            try {
                // Get HWID from local server
                const localResp = await fetch('/license-info');
                const localData = await localResp.json();
                
                currentHwid = localData.hwid || '';
                currentLicenseKey = localData.license_key || '';
                
                // Update HWID display
                document.getElementById('lic-hwid').innerHTML = 
                    '<span class="hwid">' + currentHwid + '</span>';
                
                // Check with remote server if a license exists for this HWID
                const checkResp = await fetch(LICENSE_API + '/check', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ hardware_id: currentHwid })
                });
                const result = await checkResp.json();
                
                if (result.found && result.license_key) {
                    // License found on server! Save it locally
                    await saveLicenseKey(result.license_key);
                    currentLicenseKey = result.license_key;
                    
                    // Now validate the newly saved license locally
                    const localValidResp = await fetch('/license-validate', {
                        headers: { 'X-Session-Token': window.SESSION_TOKEN }
                    });
                    const localValid = await localValidResp.json();
                    
                    if (localValid.valid) {
                        showLicenseValid({
                            message: 'License downloaded and validated',
                            license_info: localValid
                        });
                        const keyDisplay = document.getElementById('lic-key-display');
                        keyDisplay.textContent = result.license_key;
                        keyDisplay.style.display = 'block';
                    } else {
                        showLicenseInvalid('Downloaded license failed validation: ' + localValid.message);
                    }
                } else if (result.pending_request) {
                    // Request is pending
                    showLicensePending();
                } else {
                    // No license on server - check if we have a valid local one
                    if (currentLicenseKey) {
                        const localValidResp = await fetch('/license-validate', {
                            headers: { 'X-Session-Token': window.SESSION_TOKEN }
                        });
                        const localValid = await localValidResp.json();
                        
                        if (localValid.valid) {
                            showLicenseValid({
                                message: 'License validated',
                                license_info: localValid
                            });
                            const keyDisplay = document.getElementById('lic-key-display');
                            keyDisplay.textContent = currentLicenseKey;
                            keyDisplay.style.display = 'block';
                        } else {
                            showLicenseInvalid(localValid.message || 'License validation failed');
                        }
                    } else {
                        showLicenseInvalid('No license found');
                    }
                }
            } catch (err) {
                // Network error - still try to validate local license
                if (currentLicenseKey) {
                    try {
                        const localValidResp = await fetch('/license-validate', {
                            headers: { 'X-Session-Token': window.SESSION_TOKEN }
                        });
                        const localValid = await localValidResp.json();
                        
                        if (localValid.valid) {
                            showLicenseValid({
                                message: 'License validated (offline)',
                                license_info: localValid
                            });
                            const keyDisplay = document.getElementById('lic-key-display');
                            keyDisplay.textContent = currentLicenseKey;
                            keyDisplay.style.display = 'block';
                        } else {
                            showLicenseInvalid(localValid.message || 'License validation failed');
                        }
                    } catch (e) {
                        showLicenseInvalid('Could not validate license');
                    }
                } else {
                    statusDiv.className = 'license-status invalid';
                    statusDiv.innerHTML = '<h3>⚠️ Network Error</h3><p>' + err.message + '</p>' +
                        '<p>Hardware ID: <span class="hwid">' + (currentHwid || 'Unknown') + '</span></p>';
                    formDiv.style.display = 'block';
                }
            }
        }
        
        function showLicenseValid(result) {
            const statusDiv = document.getElementById('license-status');
            const infoDiv = document.getElementById('license-info');
            const formDiv = document.getElementById('license-form');
            
            statusDiv.className = 'license-status valid';
            statusDiv.innerHTML = '<h3>✓ License Valid</h3><p>' + result.message + '</p>';
            
            if (result.license_info) {
                document.getElementById('lic-customer').textContent = result.license_info.customer_name || '-';
                document.getElementById('lic-status').textContent = result.license_info.is_perpetual ? 'Perpetual' : 'Active';
                document.getElementById('lic-expiry').textContent = result.license_info.is_perpetual ? 'Never' : 
                    (result.license_info.expiry_date || '-');
            }
            
            infoDiv.style.display = 'block';
            formDiv.style.display = 'none';
        }
        
        function showLicenseInvalid(message) {
            const statusDiv = document.getElementById('license-status');
            const infoDiv = document.getElementById('license-info');
            const formDiv = document.getElementById('license-form');
            
            statusDiv.className = 'license-status invalid';
            statusDiv.innerHTML = '<h3>✗ License Invalid</h3><p>' + message + '</p>' +
                '<p>Hardware ID: <span class="hwid">' + currentHwid + '</span></p>';
            
            infoDiv.style.display = 'none';
            formDiv.style.display = 'block';
        }
        
        function showLicensePending() {
            const statusDiv = document.getElementById('license-status');
            const formDiv = document.getElementById('license-form');
            
            statusDiv.className = 'license-status pending';
            statusDiv.innerHTML = '<h3>⏳ License Request Pending</h3>' +
                '<p>Your license request is being reviewed. You will receive an email when approved.</p>' +
                '<p>Hardware ID: <span class="hwid">' + currentHwid + '</span></p>';
            
            formDiv.style.display = 'block';
        }
        
        async function checkForLicense() {
            const msgDiv = document.getElementById('license-message');
            msgDiv.className = 'upload-status';
            msgDiv.style.display = 'block';
            msgDiv.textContent = 'Checking for license...';
            msgDiv.style.background = '#1e3a5f';
            
            try {
                // Get current HWID if not set
                if (!currentHwid) {
                    const localResp = await fetch('/license-info');
                    const localData = await localResp.json();
                    currentHwid = localData.hwid || '';
                }
                
                const resp = await fetch(LICENSE_API + '/check', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        hardware_id: currentHwid,
                        email: document.getElementById('lic-email').value || ''
                    })
                });
                const result = await resp.json();
                
                if (result.found && result.license_key) {
                    // License found! Save it locally
                    await saveLicenseKey(result.license_key);
                    currentLicenseKey = result.license_key;
                    
                    msgDiv.className = 'upload-status success';
                    msgDiv.innerHTML = '✓ License found and installed!';
                    
                    // Update UI
                    showLicenseValid({
                        message: 'License installed successfully',
                        license_info: result.license_info
                    });
                    
                    // Show the license key
                    const keyDisplay = document.getElementById('lic-key-display');
                    keyDisplay.textContent = result.license_key;
                    keyDisplay.style.display = 'block';
                } else if (result.pending_request) {
                    showLicensePending();
                    msgDiv.className = 'upload-status';
                    msgDiv.style.background = '#5f5f1e';
                    msgDiv.textContent = '⏳ ' + result.message;
                } else {
                    showLicenseInvalid('No license found for this hardware ID');
                    msgDiv.className = 'upload-status error';
                    msgDiv.textContent = result.message || 'No license found';
                }
            } catch (err) {
                msgDiv.className = 'upload-status error';
                msgDiv.textContent = '✗ Error: ' + err.message;
            }
        }
        
        async function requestLicense() {
            const name = document.getElementById('lic-name').value.trim();
            const email = document.getElementById('lic-email').value.trim();
            const company = document.getElementById('lic-company').value.trim();
            const usecase = document.getElementById('lic-usecase').value.trim();
            const msgDiv = document.getElementById('license-message');
            const btn = document.getElementById('btn-request-license');
            
            // Validation
            if (!name || name.length < 2) {
                msgDiv.className = 'upload-status error';
                msgDiv.style.display = 'block';
                msgDiv.textContent = '✗ Please enter your full name';
                return;
            }
            if (!email || !email.includes('@')) {
                msgDiv.className = 'upload-status error';
                msgDiv.style.display = 'block';
                msgDiv.textContent = '✗ Please enter a valid email address';
                return;
            }
            
            btn.disabled = true;
            btn.textContent = 'Submitting...';
            msgDiv.style.display = 'block';
            msgDiv.className = 'upload-status';
            msgDiv.style.background = '#1e3a5f';
            msgDiv.textContent = 'Submitting license request...';
            
            try {
                const resp = await fetch(LICENSE_API + '/request', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        customer_name: name,
                        email: email,
                        hardware_id: currentHwid,
                        company: company,
                        use_case: usecase
                    })
                });
                const result = await resp.json();
                
                if (result.success) {
                    if (result.status === 'already_licensed' && result.license_key) {
                        // Already has a license!
                        await saveLicenseKey(result.license_key);
                        currentLicenseKey = result.license_key;
                        
                        msgDiv.className = 'upload-status success';
                        msgDiv.innerHTML = '✓ ' + result.message;
                        
                        showLicenseValid({
                            message: 'License already exists and has been installed',
                            license_info: result.license_info || {}
                        });
                    } else {
                        // Request submitted
                        msgDiv.className = 'upload-status success';
                        msgDiv.innerHTML = '✓ ' + result.message;
                        showLicensePending();
                    }
                } else {
                    msgDiv.className = 'upload-status error';
                    msgDiv.textContent = '✗ ' + (result.message || 'Request failed');
                }
            } catch (err) {
                msgDiv.className = 'upload-status error';
                msgDiv.textContent = '✗ Error: ' + err.message;
            } finally {
                btn.disabled = false;
                btn.textContent = '📤 Submit License Request';
            }
        }
        
        async function validateLicense() {
            const msgDiv = document.getElementById('license-message');
            msgDiv.style.display = 'block';
            msgDiv.className = 'upload-status';
            msgDiv.style.background = '#1e3a5f';
            msgDiv.textContent = 'Validating license...';
            
            await checkLicenseStatus();
        }
        
        async function saveLicenseKey(licenseKey) {
            try {
                const resp = await fetch('/license-save', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ license_key: licenseKey })
                });
                const result = await resp.json();
                if (!result.success) {
                    throw new Error(result.message || 'Failed to save license');
                }
            } catch (err) {
                console.error('Failed to save license:', err);
                throw err;
            }
        }
        
        // Check license status on page load
        document.addEventListener('DOMContentLoaded', function() {
            // Pre-load license info (but don't validate yet - only when tab is clicked)
            fetch('/license-info').then(r => r.json()).then(data => {
                currentHwid = data.hwid || '';
                currentLicenseKey = data.license_key || '';
            }).catch(() => {});
        });
        
        // Load reports list
        function loadReports() {
            fetch('/reports')
                .then(r => r.json())
                .then(reports => {
                    const grid = document.getElementById('reports-grid');
                    if (reports.length === 0) {
                        grid.innerHTML = '<div class="no-reports">No reports found.<br>Run a test to generate reports.</div>';
                        return;
                    }
                    
                    grid.innerHTML = reports.map(r => `
                        <div class="report-card ${r.type}">
                            <div onclick="viewReport('${r.filename}')">
                                <div class="type">${r.type}</div>
                                <div class="title">${r.filename}</div>
                                <div class="meta">
                                    <div class="meta-item"><span class="label">Date:</span> <span class="value">${r.date}</span></div>
                                    <div class="meta-item"><span class="label">Time:</span> <span class="value">${r.time}</span></div>
                                    <div class="meta-item"><span class="label">Backend:</span> <span class="value">${r.backend}</span></div>
                                    <div class="meta-item"><span class="label">Duration:</span> <span class="value">${r.duration}</span></div>
                                    <div class="meta-item"><span class="label">Version:</span> <span class="value">${r.version}</span></div>
                                    <div class="meta-item"><span class="label">Detection:</span> <span class="value">${r.detection}</span></div>
                                </div>
                            </div>
                            <div class="actions">
                                <button class="btn-card btn-upload-card" onclick="uploadSingleReport('${r.filename}', this)" title="Upload to Phoenix Nest">
                                    📤 Upload
                                </button>
                            </div>
                        </div>
                    `).join('');
                })
                .catch(err => {
                    document.getElementById('reports-grid').innerHTML = 
                        '<div class="no-reports">Error loading reports: ' + err.message + '</div>';
                });
        }
        
        // View a specific report
        function viewReport(filename) {
            const viewer = document.getElementById('report-viewer');
            viewer.innerHTML = '<p>Loading...</p>';
            viewer.classList.add('active');
            
            fetch('/report?name=' + encodeURIComponent(filename))
                .then(r => r.text())
                .then(content => {
                    // Convert markdown to basic HTML
                    let html = content
                        .replace(/^# (.+)$/gm, '<h1>$1</h1>')
                        .replace(/^## (.+)$/gm, '<h2>$1</h2>')
                        .replace(/^### (.+)$/gm, '<h3>$1</h3>')
                        .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
                        .replace(/^\| (.+) \|$/gm, (match) => {
                            const cells = match.slice(1, -1).split('|').map(c => c.trim());
                            return '<tr>' + cells.map(c => '<td>' + c + '</td>').join('') + '</tr>';
                        })
                        .replace(/^\|[-:| ]+\|$/gm, '')  // Remove separator rows
                        .replace(/(<tr>.*<\/tr>\n?)+/gs, '<table>$&</table>')
                        .replace(/^---$/gm, '<hr>')
                        .replace(/\n/g, '<br>');
                    
                    viewer.innerHTML = '<button onclick="closeViewer()" style="float:right;padding:5px 15px;">Close</button>' + html;
                })
                .catch(err => {
                    viewer.innerHTML = '<p>Error loading report: ' + err.message + '</p>';
                });
        }
        
        function closeViewer() {
            document.getElementById('report-viewer').classList.remove('active');
        }
        
        // Show popup notification
        function showPopup(message, isSuccess) {
            // Remove any existing popup
            const existing = document.querySelector('.popup');
            if (existing) existing.remove();
            
            const popup = document.createElement('div');
            popup.className = 'popup ' + (isSuccess ? 'success' : 'error');
            popup.innerHTML = message;
            document.body.appendChild(popup);
            
            // Auto-remove after 5 seconds
            setTimeout(() => popup.remove(), 5000);
        }
        
        // Upload a single report from the Reports tab
        async function uploadSingleReport(filename, btn) {
            const originalText = btn.innerHTML;
            btn.disabled = true;
            btn.innerHTML = '⏳';
            
            try {
                // Get the report content
                const reportResp = await fetch('/report?name=' + encodeURIComponent(filename));
                if (!reportResp.ok) throw new Error('Failed to fetch report');
                const reportContent = await reportResp.text();
                
                // Upload to the API
                const uploadResp = await fetch('https://www.organicengineer.com/software/api/report', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        report_content: reportContent,
                        report_filename: filename
                    })
                });
                
                const result = await uploadResp.json();
                
                if (result.success) {
                    showPopup('✓ Uploaded as <a href="' + result.issue_url + 
                        '" target="_blank">#' + result.issue_number + '</a>', true);
                    btn.innerHTML = '✓';
                    btn.style.background = '#1e5f3a';
                    setTimeout(() => {
                        btn.innerHTML = originalText;
                        btn.style.background = '';
                        btn.disabled = false;
                    }, 3000);
                } else {
                    throw new Error(result.message || 'Upload failed');
                }
            } catch (err) {
                showPopup('✗ Upload failed: ' + err.message, false);
                btn.innerHTML = '✗';
                btn.style.background = '#5f1e1e';
                setTimeout(() => {
                    btn.innerHTML = originalText;
                    btn.style.background = '';
                    btn.disabled = false;
                }, 3000);
            }
        }
        
        // Support tab functions
        const SUPPORT_URLS = {
            'issues': 'https://www.organicengineer.com/software/issues',
            'bug': 'https://www.organicengineer.com/software/issues/new/bug',
            'feature': 'https://www.organicengineer.com/software/issues/new/feature',
            'question': 'https://www.organicengineer.com/software/issues/new/question',
            'report': 'https://www.organicengineer.com/software/issues/new/report',
            'docs': 'https://www.organicengineer.com/software/docs/'
        };
        
        function openSupport(type) {
            const iframe = document.getElementById('support-iframe');
            iframe.src = SUPPORT_URLS[type] || SUPPORT_URLS['issues'];
        }
        
        async function uploadReport() {
            const btn = document.getElementById('btn-upload');
            const statusDiv = document.getElementById('upload-status');
            
            btn.disabled = true;
            btn.textContent = 'Uploading...';
            statusDiv.className = 'upload-status';
            statusDiv.style.display = 'none';
            
            try {
                // First get the latest report from our server
                const reportsResp = await fetch('/reports');
                const reports = await reportsResp.json();
                
                if (reports.length === 0) {
                    throw new Error('No reports available. Run a test first.');
                }
                
                // Get the most recent report
                const latestReport = reports[0];
                const reportResp = await fetch('/report?name=' + encodeURIComponent(latestReport.filename));
                const reportContent = await reportResp.text();
                
                // Upload to the API
                const uploadResp = await fetch('https://www.organicengineer.com/software/api/report', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        report_content: reportContent,
                        report_filename: latestReport.filename
                    })
                });
                
                const result = await uploadResp.json();
                
                if (result.success) {
                    statusDiv.className = 'upload-status success';
                    statusDiv.innerHTML = '✓ Report uploaded as <a href=\"' + result.issue_url + 
                        '\" target=\"_blank\" style=\"color: #00d4ff;\">#' + result.issue_number + '</a>';
                    // Refresh the iframe to show the new issue
                    document.getElementById('support-iframe').src = result.issue_url;
                } else {
                    throw new Error(result.message || 'Upload failed');
                }
            } catch (err) {
                statusDiv.className = 'upload-status error';
                statusDiv.textContent = '✗ ' + err.message;
            } finally {
                btn.disabled = false;
                btn.textContent = '📊 Upload Diagnostic Report';
            }
        }
        
        // Get selected values from a multi-select
        function getSelected(id) {
            const sel = document.getElementById(id);
            return Array.from(sel.selectedOptions).map(o => o.value);
        }
        
        // Set selected values in a multi-select
        function setSelected(id, values) {
            const sel = document.getElementById(id);
            Array.from(sel.options).forEach(o => o.selected = values.includes(o.value));
            updateSummary();
        }
        
        // Quick select helpers
        function selectAllModes() { setSelected('modes', ALL_MODES); }
        function selectShortModes() { setSelected('modes', ALL_MODES.filter(m => m.endsWith('S'))); }
        function selectLongModes() { setSelected('modes', ALL_MODES.filter(m => m.endsWith('L'))); }
        function clearModes() { setSelected('modes', []); }
        function selectAllEqualizers() { setSelected('equalizers', ALL_EQS); }
        function clearEqualizers() { setSelected('equalizers', []); }
        
        // Update test summary
        function updateSummary() {
            const modes = getSelected('modes');
            const eqs = getSelected('equalizers');
            const modeCount = modes.length || 12;
            const eqCount = eqs.length || 1;
            const totalTests = modeCount * eqCount;
            
            const modeStr = modes.length === 0 ? 'All modes' : 
                           modes.length === 12 ? 'All modes' :
                           modes.length <= 3 ? modes.join(', ') : 
                           modes.length + ' modes';
            const eqStr = eqs.length === 0 ? 'DFE' : 
                         eqs.length === 7 ? 'All equalizers' :
                         eqs.length <= 2 ? eqs.join(', ') : 
                         eqs.length + ' equalizers';
            
            document.getElementById('test-summary').innerHTML = 
                'Will run: <strong>' + modeStr + '</strong> with <strong>' + eqStr + '</strong> (' + totalTests + ' test combinations)';
        }
        
        // Show/hide progressive options
        document.querySelectorAll('input[name="testtype"]').forEach(radio => {
            radio.addEventListener('change', () => {
                const testType = document.querySelector('input[name="testtype"]:checked').value;
                document.getElementById('prog-options').style.display = 
                    testType === 'progressive' ? 'flex' : 'none';
                document.getElementById('ref-options').style.display = 
                    testType === 'reference' ? 'flex' : 'none';
                
                // Disable mode/eq selection for reference tests
                const isRef = testType === 'reference';
                document.getElementById('modes').disabled = isRef;
                document.getElementById('equalizers').disabled = isRef;
                document.getElementById('iterations').disabled = isRef;
                updateSummary();
            });
        });
        
        // Update summary when selections change
        document.getElementById('modes').addEventListener('change', updateSummary);
        document.getElementById('equalizers').addEventListener('change', updateSummary);
        
        // Enable/disable threads based on backend selection
        document.getElementById('backend').addEventListener('change', () => {
            const isServer = document.getElementById('backend').value === 'server';
            document.getElementById('threads').disabled = isServer;
            if (isServer) {
                document.getElementById('threads').value = '1';
            }
        });
        
        function runTest() {
            const output = document.getElementById('output');
            const status = document.getElementById('status');
            const btnRun = document.getElementById('btn-run');
            const btnStop = document.getElementById('btn-stop');
            
            const testType = document.querySelector('input[name="testtype"]:checked').value;
            
            // Build command based on test type
            let args = [];
            
            if (testType === 'reference') {
                // Reference sample test - simple command
                args.push('--reference');
                
                // Still use selected equalizers if any
                let eqs = getSelected('equalizers');
                if (eqs.length > 0) {
                    args.push('--eqs', eqs.join(','));
                }
            } else {
                // Standard or Progressive tests
                // Get selected modes and equalizers
                let modes = getSelected('modes');
                let eqs = getSelected('equalizers');
                
                // Default to all modes if none selected
                if (modes.length === 0) modes = ALL_MODES;
                // Default to DFE if none selected
                if (eqs.length === 0) eqs = ['DFE'];
                
                args.push('--modes', modes.join(','));
                args.push('--eqs', eqs.join(','));
                
                const iters = document.getElementById('iterations').value;
                args.push('-n', iters);
            }
            
            // Add parallel threads (only for direct backend)
            const backend = document.getElementById('backend').value;
            if (backend === 'direct') {
                const threads = document.getElementById('threads').value;
                if (threads > 1) {
                    args.push('-j', threads);
                }
            }
            
            if (backend === 'server') {
                args.push('--server');
            }
            
            // Add auto-detect flag if selected
            const detection = document.getElementById('detection').value;
            if (detection === 'auto') {
                args.push('--use-auto-detect');
            }
            
            if (testType === 'progressive') {
                if (document.getElementById('prog-snr').checked &&
                    document.getElementById('prog-freq').checked &&
                    document.getElementById('prog-multipath').checked) {
                    args.push('-p');
                } else {
                    if (document.getElementById('prog-snr').checked) args.push('--prog-snr');
                    if (document.getElementById('prog-freq').checked) args.push('--prog-freq');
                    if (document.getElementById('prog-multipath').checked) args.push('--prog-multipath');
                }
                
                // Add CSV output if checkbox is checked
                if (document.getElementById('csv-output').checked) {
                    const csvFile = document.getElementById('csv-filename').value || 'progressive_results.csv';
                    args.push('--csv', csvFile);
                }
            }
            
            // Clear output
            output.textContent = 'Starting test...\n\n';
            status.textContent = 'Running...';
            status.className = 'status status-running';
            btnRun.disabled = true;
            btnStop.disabled = false;
            
            // Start SSE connection
            const url = '/run?' + args.map(a => 'arg=' + encodeURIComponent(a)).join('&');
            eventSource = new EventSource(url);
            
            eventSource.onmessage = function(e) {
                output.textContent += e.data + '\n';
                output.scrollTop = output.scrollHeight;
            };
            
            eventSource.addEventListener('done', function(e) {
                const result = e.data;
                eventSource.close();
                eventSource = null;
                btnRun.disabled = false;
                btnStop.disabled = true;
                
                if (result.includes('PASS') || result.includes('100.0%')) {
                    status.textContent = 'Complete - PASSED';
                    status.className = 'status status-pass';
                } else if (result.includes('FAIL')) {
                    status.textContent = 'Complete - FAILED';
                    status.className = 'status status-fail';
                } else {
                    status.textContent = 'Complete';
                    status.className = 'status status-idle';
                }
            });
            
            eventSource.onerror = function() {
                eventSource.close();
                eventSource = null;
                btnRun.disabled = false;
                btnStop.disabled = true;
                status.textContent = 'Error or Disconnected';
                status.className = 'status status-fail';
            };
        }
        
        function stopTest() {
            if (eventSource) {
                eventSource.close();
                eventSource = null;
            }
            fetch('/stop');
            document.getElementById('btn-run').disabled = false;
            document.getElementById('btn-stop').disabled = true;
            document.getElementById('status').textContent = 'Stopped';
            document.getElementById('status').className = 'status status-idle';
        }
    </script>
</body>
</html>
)HTML";

class TestGuiServer {
public:
    TestGuiServer(int port = 8080) : port_(port), running_(false), test_process_(nullptr) {
        // Get directory of this executable
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
        // Generate random session token for endpoint protection
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
        
        // Try to open browser automatically
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
    std::string session_token_;  // Random token for endpoint protection
    SOCKET server_sock_;
    bool running_;
    FILE* test_process_;
    
    void handle_client(SOCKET client) {
        char buffer[4096];
        int n = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            closesocket(client);
            return;
        }
        buffer[n] = '\0';
        
        std::string request(buffer);
        
        // Parse request line
        std::string method, path;
        std::istringstream iss(request);
        iss >> method >> path;
        
        if (path == "/" || path == "/index.html") {
            send_html(client, HTML_PAGE);
        } else if (path.find("/run?") == 0) {
            handle_run(client, path);
        } else if (path == "/stop") {
            handle_stop(client);
        } else if (path == "/reports") {
            handle_reports_list(client);
        } else if (path.find("/report?") == 0) {
            handle_report_content(client, path);
        } else if (path == "/license-info") {
            handle_license_info(client);
        } else if (path == "/license-validate") {
            // Protected endpoint - require session token
            if (!verify_session_token(request)) {
                send_403(client, "Invalid or missing session token");
            } else {
                handle_license_validate(client);
            }
        } else if (path == "/license-save" && method == "POST") {
            handle_license_save(client, request);
        } else {
            send_404(client);
        }
        
        closesocket(client);
    }
    
    void send_html(SOCKET client, const char* html) {
        // Inject session token into HTML for protected endpoint access
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
    
    void send_403(SOCKET client, const char* message) {
        std::ostringstream body;
        body << "{\"error\":\"Forbidden\",\"message\":\"" << message << "\"}";
        std::string body_str = body.str();
        std::ostringstream response;
        response << "HTTP/1.1 403 Forbidden\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << body_str.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << body_str;
        std::string resp = response.str();
        send(client, resp.c_str(), (int)resp.size(), 0);
    }
    
    bool verify_session_token(const std::string& request) {
        // Look for X-Session-Token header
        std::string header = "X-Session-Token: ";
        size_t pos = request.find(header);
        if (pos == std::string::npos) return false;
        
        size_t start = pos + header.length();
        size_t end = request.find("\r\n", start);
        if (end == std::string::npos) end = request.length();
        
        std::string token = request.substr(start, end - start);
        return token == session_token_;
    }

    void handle_run(SOCKET client, const std::string& path) {
        // Parse arguments from query string
        std::vector<std::string> args;
        size_t pos = path.find('?');
        if (pos != std::string::npos) {
            std::string query = path.substr(pos + 1);
            std::istringstream iss(query);
            std::string param;
            while (std::getline(iss, param, '&')) {
                if (param.find("arg=") == 0) {
                    std::string val = param.substr(4);
                    // URL decode
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
                    args.push_back(decoded);
                }
            }
        }
        
        // Build command line - cd to exe directory first, then run
        // This ensures license.key is found in the same directory as the exe
        std::string cmd = "cd /d \"" + exe_dir_ + "\" && \"" + exe_dir_ + "exhaustive_test.exe\"";
        for (const auto& arg : args) {
            cmd += " " + arg;
        }
        
        // Send SSE headers
        std::string headers = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "\r\n";
        send(client, headers.c_str(), (int)headers.size(), 0);
        
        // Run test and stream output
#ifdef _WIN32
        test_process_ = _popen(cmd.c_str(), "r");
#else
        test_process_ = popen(cmd.c_str(), "r");
#endif
        
        if (!test_process_) {
            std::string msg = "data: ERROR: Could not start test process\n\n";
            send(client, msg.c_str(), (int)msg.size(), 0);
            return;
        }
        
        char line[1024];
        std::string last_line;
        while (fgets(line, sizeof(line), test_process_)) {
            // Remove trailing newline
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
                line[--len] = '\0';
            }
            
            last_line = line;
            
            // Send as SSE data
            std::string msg = "data: " + std::string(line) + "\n\n";
            if (send(client, msg.c_str(), (int)msg.size(), 0) < 0) {
                break;  // Client disconnected
            }
        }
        
#ifdef _WIN32
        _pclose(test_process_);
#else
        pclose(test_process_);
#endif
        test_process_ = nullptr;
        
        // Send done event
        std::string done = "event: done\ndata: " + last_line + "\n\n";
        send(client, done.c_str(), (int)done.size(), 0);
    }
    
    void handle_stop(SOCKET client) {
        // TODO: Actually kill the test process
        const char* response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n"
            "\r\n"
            "OK";
        send(client, response, (int)strlen(response), 0);
    }
    
    void handle_reports_list(SOCKET client) {
        std::string reports_dir = exe_dir_ + "reports";
        std::ostringstream json;
        json << "[";
        
        bool first = true;
        if (fs::exists(reports_dir)) {
            std::vector<std::pair<fs::file_time_type, std::string>> files;
            
            for (const auto& entry : fs::directory_iterator(reports_dir)) {
                if (entry.path().extension() == ".md") {
                    files.push_back({entry.last_write_time(), entry.path().filename().string()});
                }
            }
            
            // Sort by time descending (newest first)
            std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
                return a.first > b.first;
            });
            
            for (const auto& [time, filename] : files) {
                // Parse metadata from filename and file content
                std::string type = "exhaustive";
                std::string backend = "direct";
                std::string date = "";
                std::string time_str = "";
                std::string version = "";
                std::string duration = "";
                std::string detection = "KNOWN";
                
                // Parse filename: progressive_direct_20251209_143052.md
                if (filename.find("progressive") != std::string::npos) type = "progressive";
                if (filename.find("server") != std::string::npos) backend = "server";
                
                // Extract date/time from filename
                std::regex dt_regex("(\\d{4})(\\d{2})(\\d{2})_(\\d{2})(\\d{2})(\\d{2})");
                std::smatch match;
                if (std::regex_search(filename, match, dt_regex)) {
                    date = match[1].str() + "-" + match[2].str() + "-" + match[3].str();
                    time_str = match[4].str() + ":" + match[5].str() + ":" + match[6].str();
                }
                
                // Read file to extract metadata
                std::ifstream file(reports_dir + "/" + filename);
                std::string line;
                while (std::getline(file, line)) {
                    if (line.find("**Version**") != std::string::npos) {
                        size_t pos = line.find_last_of("|");
                        if (pos > 1) {
                            size_t start = line.rfind("|", pos - 1);
                            if (start != std::string::npos) {
                                version = line.substr(start + 1, pos - start - 1);
                                // Trim
                                version.erase(0, version.find_first_not_of(" "));
                                version.erase(version.find_last_not_of(" ") + 1);
                            }
                        }
                    }
                    if (line.find("**Duration**") != std::string::npos) {
                        size_t pos = line.find_last_of("|");
                        if (pos > 1) {
                            size_t start = line.rfind("|", pos - 1);
                            if (start != std::string::npos) {
                                duration = line.substr(start + 1, pos - start - 1);
                                duration.erase(0, duration.find_first_not_of(" "));
                                duration.erase(duration.find_last_not_of(" ") + 1);
                            }
                        }
                    }
                    if (line.find("**Mode Detection**") != std::string::npos) {
                        if (line.find("AUTO") != std::string::npos) detection = "AUTO";
                    }
                    // Stop after reading header
                    if (line.find("---") != std::string::npos && version != "") break;
                }
                
                if (!first) json << ",";
                first = false;
                
                json << "{";
                json << "\"filename\":\"" << filename << "\",";
                json << "\"type\":\"" << type << "\",";
                json << "\"backend\":\"" << backend << "\",";
                json << "\"date\":\"" << date << "\",";
                json << "\"time\":\"" << time_str << "\",";
                json << "\"version\":\"" << version << "\",";
                json << "\"duration\":\"" << duration << "\",";
                json << "\"detection\":\"" << detection << "\"";
                json << "}";
            }
        }
        
        json << "]";
        
        std::string body = json.str();
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << body;
        std::string resp = response.str();
        send(client, resp.c_str(), (int)resp.size(), 0);
    }
    
    void handle_report_content(SOCKET client, const std::string& path) {
        // Parse filename from query string
        std::string filename;
        size_t pos = path.find("name=");
        if (pos != std::string::npos) {
            filename = path.substr(pos + 5);
            // URL decode
            std::string decoded;
            for (size_t i = 0; i < filename.size(); i++) {
                if (filename[i] == '%' && i + 2 < filename.size()) {
                    int hex;
                    sscanf(filename.substr(i + 1, 2).c_str(), "%x", &hex);
                    decoded += (char)hex;
                    i += 2;
                } else if (filename[i] == '+') {
                    decoded += ' ';
                } else {
                    decoded += filename[i];
                }
            }
            filename = decoded;
        }
        
        // Validate filename (no path traversal)
        if (filename.find("..") != std::string::npos || filename.find("/") != std::string::npos) {
            send_404(client);
            return;
        }
        
        std::string filepath = exe_dir_ + "reports/" + filename;
        std::ifstream file(filepath);
        if (!file.is_open()) {
            send_404(client);
            return;
        }
        
        std::ostringstream content;
        content << file.rdbuf();
        std::string body = content.str();
        
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: text/plain\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << body;
        std::string resp = response.str();
        send(client, resp.c_str(), (int)resp.size(), 0);
    }
    
    void handle_license_info(SOCKET client) {
        // Get HWID using LicenseManager
        std::string hwid = m110a::LicenseManager::get_hardware_id();
        
        // Try to read existing license key
        std::string license_key;
        std::string license_path = exe_dir_ + "license.key";
        std::ifstream lic_file(license_path);
        if (lic_file.is_open()) {
            std::getline(lic_file, license_key);
            // Trim whitespace
            size_t start = license_key.find_first_not_of(" \t\r\n");
            size_t end = license_key.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                license_key = license_key.substr(start, end - start + 1);
            }
        }
        
        // Build JSON response
        std::ostringstream json;
        json << "{\"hwid\":\"" << hwid << "\",\"license_key\":\"" << license_key << "\"}";
        std::string body = json.str();
        
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << body;
        std::string resp = response.str();
        send(client, resp.c_str(), (int)resp.size(), 0);
    }
    
    void handle_license_save(SOCKET client, const std::string& request) {
        // Find the JSON body
        size_t body_start = request.find("\r\n\r\n");
        std::string body;
        if (body_start != std::string::npos) {
            body = request.substr(body_start + 4);
        }
        
        // Parse license_key from JSON (simple parsing)
        std::string license_key;
        size_t key_pos = body.find("\"license_key\"");
        if (key_pos != std::string::npos) {
            size_t colon = body.find(':', key_pos);
            size_t quote1 = body.find('"', colon);
            size_t quote2 = body.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                license_key = body.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
        
        std::string response_body;
        if (license_key.empty()) {
            response_body = "{\"success\":false,\"message\":\"No license key provided\"}";
        } else {
            // Save to file
            std::string license_path = exe_dir_ + "license.key";
            std::ofstream lic_file(license_path);
            if (lic_file.is_open()) {
                lic_file << license_key;
                lic_file.close();
                response_body = "{\"success\":true,\"message\":\"License saved successfully\"}";
            } else {
                response_body = "{\"success\":false,\"message\":\"Failed to write license file\"}";
            }
        }
        
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << response_body.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << response_body;
        std::string resp = response.str();
        send(client, resp.c_str(), (int)resp.size(), 0);
    }
    
    void handle_license_validate(SOCKET client) {
        // Use the actual LicenseManager to validate the license
        std::string license_path = exe_dir_ + "license.key";
        m110a::LicenseInfo info;
        m110a::LicenseStatus status = m110a::LicenseManager::load_license_file(license_path, info);
        
        std::ostringstream json;
        if (status == m110a::LicenseStatus::VALID) {
            // Format expiration date
            char date_buf[32];
            std::tm* tm_info = std::localtime(&info.expiration_date);
            std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm_info);
            
            json << "{\"valid\":true,\"message\":\"License is valid\""
                 << ",\"customer_id\":\"" << info.customer_id << "\""
                 << ",\"is_trial\":" << (info.is_trial ? "true" : "false")
                 << ",\"expiration_date\":\"" << date_buf << "\""
                 << ",\"max_channels\":" << info.max_channels
                 << "}";
        } else {
            std::string message = m110a::LicenseManager::get_status_message(status);
            // Escape quotes in message
            std::string escaped;
            for (char c : message) {
                if (c == '"') escaped += "\\\"";
                else if (c == '\\') escaped += "\\\\";
                else escaped += c;
            }
            json << "{\"valid\":false,\"message\":\"" << escaped << "\"}";
        }
        
        std::string body = json.str();
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << body;
        std::string resp = response.str();
        send(client, resp.c_str(), (int)resp.size(), 0);
    }
};

int main(int argc, char* argv[]) {
    int port = 8080;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "M110A Test GUI Server\n\n";
            std::cout << "Usage: " << argv[0] << " [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --port N, -p N   HTTP port (default: 8080)\n";
            std::cout << "  --help, -h       Show this help\n\n";
            std::cout << "Opens a web browser to control the test suite.\n";
            return 0;
        }
    }
    
    TestGuiServer server(port);
    server.start();
    
    return 0;
}
