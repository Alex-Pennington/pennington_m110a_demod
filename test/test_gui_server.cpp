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
                    font-weight: bold; display: flex; align-items: center; gap: 8px; }
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
                      font-weight: bold; display: flex; align-items: center; gap: 8px; }
        .btn-record-start { background: #ff4757; color: #fff; }
        .btn-record-start:hover { background: #cc3a47; }
        .btn-record-start.recording { background: #ff0000; animation: pulse 1s infinite; }
        .btn-record-stop { background: #444; color: #fff; }
        .btn-record-stop:hover { background: #555; }
        .btn-record-save { background: #5fff5f; color: #000; }
        .btn-record-save:hover { background: #4acc4a; }
        .btn-record-save:disabled { background: #444; color: #888; cursor: not-allowed; }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.7; } }
        .record-status { margin-left: 15px; color: #aaa; font-size: 13px; }
        .record-status.recording { color: #ff4757; font-weight: bold; }
        .record-timer { font-family: 'Consolas', monospace; font-size: 16px; color: #ff4757; margin-left: 10px; }
        .record-name-input { padding: 8px 12px; border: 1px solid #333; border-radius: 4px; 
                             background: #0f0f23; color: #fff; width: 200px; }
        .custom-file-marker { color: #ff9f43; font-size: 11px; margin-left: 5px; }
        
        /* MS-DMT Interop styles */
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
        
        /* Sub-tab navigation for MS-DMT Interop */
        .sub-tabs { display: flex; gap: 10px; margin-bottom: 20px; flex-wrap: wrap; }
        .sub-tab { padding: 10px 20px; border: 1px solid #333; border-radius: 20px; 
                   background: #16213e; color: #888; cursor: pointer; font-size: 13px;
                   transition: all 0.2s ease; }
        .sub-tab:hover { background: #1e3a5f; color: #fff; }
        .sub-tab.active { background: #00d4ff; color: #000; border-color: #00d4ff; font-weight: bold; }
        .sub-tab-content { display: none; }
        .sub-tab-content.active { display: block; }
    </style>
</head>
<body>
    <div class="container">
        <h1>M110A Modem Test Suite</h1>
        
        <div class="tabs">
            <button class="tab active" onclick="showTab('tests')">Run Tests</button>
            <button class="tab" onclick="showTab('melpe')">MELPe Vocoder</button>
            <button class="tab" onclick="showTab('interop')">MS-DMT Interop</button>
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
        
        <div id="tab-melpe" class="tab-content">
            <div class="melpe-container">
                <div class="melpe-header">
                    <h2>🎤 MELPe Vocoder Test</h2>
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
                            🔄 Run Loopback Test
                        </button>
                    </div>
                </div>
                
                <div class="record-section">
                    <h4>🎙️ Record Your Own Audio</h4>
                    <div class="record-controls">
                        <button class="btn-record btn-record-start" id="btn-record" onclick="toggleRecording()">
                            🎤 Start Recording
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
        
        <div id="tab-interop" class="tab-content">
            <div class="controls">
                <!-- Sub-tab Navigation -->
                <div class="sub-tabs">
                    <button class="sub-tab active" onclick="showSubTab('setup')">🔧 Connection Setup</button>
                    <button class="sub-tab" onclick="showSubTab('single')">🧪 Single Tests</button>
                    <button class="sub-tab" onclick="showSubTab('matrix')">📊 Matrix Test</button>
                    <button class="sub-tab" onclick="showSubTab('reference')">📂 Reference Tests</button>
                </div>
                
                <!-- Sub-tab: Connection Setup -->
                <div id="subtab-setup" class="sub-tab-content active">
                <div class="interop-section">
                    <h3>🚀 PhoenixNest Server</h3>
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
                
                <div class="interop-section">
                    <h3>🔌 MS-DMT Connection</h3>
                    <p style="color:#aaa; margin-bottom:15px; font-size:13px;">
                        Configure MS-DMT connection. <strong>Important:</strong> MS-DMT must be running with <code>--testdevices</code> flag.
                    </p>
                    <div class="interop-config">
                        <div class="interop-field">
                            <label>MS-DMT Host</label>
                            <input type="text" id="msdmt-host" value="localhost" />
                        </div>
                        <div class="interop-field">
                            <label>Control Port</label>
                            <input type="number" id="msdmt-ctrl-port" value="4999" />
                        </div>
                        <div class="interop-field">
                            <label>Data Port</label>
                            <input type="number" id="msdmt-data-port" value="4998" />
                        </div>
                    </div>
                    <div class="interop-config" style="margin-top:10px;">
                        <div class="interop-field" style="width:100%;">
                            <label>MS-DMT TX Output Dir (where MS-DMT saves TX PCM files)</label>
                            <input type="text" id="msdmt-tx-dir" value="D:\\MS-DMT_BACKUP\\Qt MSDMT Project-20240607T102834Z-001\\Qt MSDMT Project\\MS-DMT_v3.00_Beta_2.22 Qt6_Wi_Linux\\build\\tx_pcm_out" style="width:100%;" />
                        </div>
                    </div>
                    <div class="interop-config" style="margin-top:10px;">
                        <div class="interop-field" style="width:100%;">
                            <label>PhoenixNest RX Input Dir (where PhoenixNest reads PCM files)</label>
                            <input type="text" id="pn-rx-dir" value="D:\\pennington_m110a_demod\\rx_pcm_in" style="width:100%;" />
                        </div>
                    </div>
                    <div class="interop-status">
                        <span class="status-dot disconnected" id="msdmt-status-dot"></span>
                        <span id="msdmt-status-text">Disconnected</span>
                        <button class="btn-connect" id="btn-msdmt-connect" onclick="toggleMsdmtConnection()">
                            Connect to MS-DMT
                        </button>
                    </div>
                </div>
                </div><!-- end subtab-setup -->
                
                <!-- Sub-tab: Single Tests -->
                <div id="subtab-single" class="sub-tab-content">
                <div class="test-direction">
                    <h4>📤 Test 1: MS-DMT TX → PhoenixNest RX</h4>
                    <p style="color:#888; font-size:12px; margin-bottom:15px;">
                        MS-DMT generates TX audio, PhoenixNest decodes it. Validates MS-DMT transmitter.
                    </p>
                    <div class="test-controls">
                        <div class="field">
                            <label>Mode</label>
                            <select id="interop-mode-1">
                                <option value="75S">75 bps Short</option>
                                <option value="75L">75 bps Long</option>
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
                            <input type="text" id="interop-msg-1" value="HELLO INTEROP TEST" style="width:250px;" />
                        </div>
                        <button class="btn-run" id="btn-test1" onclick="runInteropTest1()" disabled>
                            ▶ Run Test
                        </button>
                    </div>
                    <ul class="test-steps" id="test1-steps">
                        <li><span class="step-icon step-pending">○</span> Set MS-DMT data rate</li>
                        <li><span class="step-icon step-pending">○</span> Enable TX recording</li>
                        <li><span class="step-icon step-pending">○</span> Send test message</li>
                        <li><span class="step-icon step-pending">○</span> Trigger SENDBUFFER</li>
                        <li><span class="step-icon step-pending">○</span> Wait for TX:IDLE</li>
                        <li><span class="step-icon step-pending">○</span> Find TX PCM file</li>
                        <li><span class="step-icon step-pending">○</span> Connect to PhoenixNest server</li>
                        <li><span class="step-icon step-pending">○</span> Inject PCM into PhoenixNest RX</li>
                        <li><span class="step-icon step-pending">○</span> Wait for DCD</li>
                        <li><span class="step-icon step-pending">○</span> Read decoded data</li>
                        <li><span class="step-icon step-pending">○</span> Wait for NO DCD</li>
                        <li><span class="step-icon step-pending">○</span> Compare output</li>
                    </ul>
                    <div class="test-result pending" id="test1-result">
                        Result will appear here after test completes
                    </div>
                </div>
                
                <div class="test-direction">
                    <h4>📥 Test 2: PhoenixNest TX → MS-DMT RX</h4>
                    <p style="color:#888; font-size:12px; margin-bottom:15px;">
                        PhoenixNest generates TX audio (48kHz), MS-DMT decodes it. Validates PhoenixNest transmitter.
                    </p>
                    <div class="test-controls">
                        <div class="field">
                            <label>Mode</label>
                            <select id="interop-mode-2">
                                <option value="75S">75 bps Short</option>
                                <option value="75L">75 bps Long</option>
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
                            <input type="text" id="interop-msg-2" value="HELLO INTEROP TEST" style="width:250px;" />
                        </div>
                        <button class="btn-run" id="btn-test2" onclick="runInteropTest2()" disabled>
                            ▶ Run Test
                        </button>
                    </div>
                    <ul class="test-steps" id="test2-steps">
                        <li><span class="step-icon step-pending">○</span> Connect to PhoenixNest server</li>
                        <li><span class="step-icon step-pending">○</span> Set PhoenixNest data rate</li>
                        <li><span class="step-icon step-pending">○</span> Enable TX recording</li>
                        <li><span class="step-icon step-pending">○</span> Send test message</li>
                        <li><span class="step-icon step-pending">○</span> Trigger SENDBUFFER</li>
                        <li><span class="step-icon step-pending">○</span> Wait for TX:IDLE</li>
                        <li><span class="step-icon step-pending">○</span> Find TX PCM file</li>
                        <li><span class="step-icon step-pending">○</span> Inject PCM into MS-DMT RX</li>
                        <li><span class="step-icon step-pending">○</span> Wait for STATUS:RX:&lt;mode&gt;</li>
                        <li><span class="step-icon step-pending">○</span> Read decoded data</li>
                        <li><span class="step-icon step-pending">○</span> Wait for NO DCD</li>
                        <li><span class="step-icon step-pending">○</span> Compare output</li>
                    </ul>
                    <div class="test-result pending" id="test2-result">
                        Result will appear here after test completes
                    </div>
                </div>
                </div><!-- end subtab-single -->
                
                <!-- Sub-tab: Matrix Test -->
                <div id="subtab-matrix" class="sub-tab-content">
                <div class="matrix-container">
                    <h3 style="color:#00d4ff; margin:0 0 15px 0;">📊 Full Compatibility Matrix</h3>
                    <div class="test-controls" style="margin-bottom:15px;">
                        <button class="btn-run" id="btn-matrix" onclick="runFullMatrix()" disabled>
                            ▶ Run All Tests (24 total)
                        </button>
                        <button class="btn-stop" id="btn-matrix-stop" onclick="stopMatrixTest()" style="display:none; background:#c00; margin-left:10px;">
                            ■ Stop
                        </button>
                        <span id="matrix-progress" style="color:#aaa;">Progress: 0/24</span>
                    </div>
                    <table class="matrix-table">
                        <thead>
                            <tr>
                                <th>Mode</th>
                                <th>MS-DMT → PN</th>
                                <th>PN → MS-DMT</th>
                            </tr>
                        </thead>
                        <tbody id="matrix-body">
                            <tr><td>75S</td><td class="matrix-cell matrix-pending" id="m-75S-1">○</td><td class="matrix-cell matrix-pending" id="m-75S-2">○</td></tr>
                            <tr><td>75L</td><td class="matrix-cell matrix-pending" id="m-75L-1">○</td><td class="matrix-cell matrix-pending" id="m-75L-2">○</td></tr>
                            <tr><td>150S</td><td class="matrix-cell matrix-pending" id="m-150S-1">○</td><td class="matrix-cell matrix-pending" id="m-150S-2">○</td></tr>
                            <tr><td>150L</td><td class="matrix-cell matrix-pending" id="m-150L-1">○</td><td class="matrix-cell matrix-pending" id="m-150L-2">○</td></tr>
                            <tr><td>300S</td><td class="matrix-cell matrix-pending" id="m-300S-1">○</td><td class="matrix-cell matrix-pending" id="m-300S-2">○</td></tr>
                            <tr><td>300L</td><td class="matrix-cell matrix-pending" id="m-300L-1">○</td><td class="matrix-cell matrix-pending" id="m-300L-2">○</td></tr>
                            <tr><td>600S</td><td class="matrix-cell matrix-pending" id="m-600S-1">○</td><td class="matrix-cell matrix-pending" id="m-600S-2">○</td></tr>
                            <tr><td>600L</td><td class="matrix-cell matrix-pending" id="m-600L-1">○</td><td class="matrix-cell matrix-pending" id="m-600L-2">○</td></tr>
                            <tr><td>1200S</td><td class="matrix-cell matrix-pending" id="m-1200S-1">○</td><td class="matrix-cell matrix-pending" id="m-1200S-2">○</td></tr>
                            <tr><td>1200L</td><td class="matrix-cell matrix-pending" id="m-1200L-1">○</td><td class="matrix-cell matrix-pending" id="m-1200L-2">○</td></tr>
                            <tr><td>2400S</td><td class="matrix-cell matrix-pending" id="m-2400S-1">○</td><td class="matrix-cell matrix-pending" id="m-2400S-2">○</td></tr>
                            <tr><td>2400L</td><td class="matrix-cell matrix-pending" id="m-2400L-1">○</td><td class="matrix-cell matrix-pending" id="m-2400L-2">○</td></tr>
                        </tbody>
                    </table>
                </div>
                </div><!-- end subtab-matrix -->
                
                <!-- Sub-tab: Reference Tests -->
                <div id="subtab-reference" class="sub-tab-content">
                <div class="matrix-container">
                    <h3 style="color:#00d4ff; margin:0 0 15px 0;">📂 MS-DMT Reference PCM Decode Test</h3>
                    <p style="color:#888; font-size:12px; margin-bottom:15px;">
                        Test MS-DMT decoding against known-good reference PCM files generated by MS-DMT itself.
                        These files contain verified M110A signals at 48kHz.
                    </p>
                    <div class="test-controls" style="margin-bottom:15px;">
                        <button class="btn-run" id="btn-ref-pcm" onclick="runRefPcmTest()" disabled>
                            ▶ Test All Reference PCMs
                        </button>
                        <span id="ref-pcm-progress" style="color:#aaa; margin-left:10px;">Progress: 0/12</span>
                    </div>
                    <table class="matrix-table">
                        <thead>
                            <tr>
                                <th>Mode</th>
                                <th>MS-DMT Decode</th>
                                <th>Expected</th>
                                <th>Decoded</th>
                            </tr>
                        </thead>
                        <tbody id="ref-pcm-body">
                            <tr><td>75S</td><td class="matrix-cell matrix-pending" id="ref-75S">○</td><td id="ref-75S-exp">-</td><td id="ref-75S-dec">-</td></tr>
                            <tr><td>75L</td><td class="matrix-cell matrix-pending" id="ref-75L">○</td><td id="ref-75L-exp">-</td><td id="ref-75L-dec">-</td></tr>
                            <tr><td>150S</td><td class="matrix-cell matrix-pending" id="ref-150S">○</td><td id="ref-150S-exp">-</td><td id="ref-150S-dec">-</td></tr>
                            <tr><td>150L</td><td class="matrix-cell matrix-pending" id="ref-150L">○</td><td id="ref-150L-exp">-</td><td id="ref-150L-dec">-</td></tr>
                            <tr><td>300S</td><td class="matrix-cell matrix-pending" id="ref-300S">○</td><td id="ref-300S-exp">-</td><td id="ref-300S-dec">-</td></tr>
                            <tr><td>300L</td><td class="matrix-cell matrix-pending" id="ref-300L">○</td><td id="ref-300L-exp">-</td><td id="ref-300L-dec">-</td></tr>
                            <tr><td>600S</td><td class="matrix-cell matrix-pending" id="ref-600S">○</td><td id="ref-600S-exp">-</td><td id="ref-600S-dec">-</td></tr>
                            <tr><td>600L</td><td class="matrix-cell matrix-pending" id="ref-600L">○</td><td id="ref-600L-exp">-</td><td id="ref-600L-dec">-</td></tr>
                            <tr><td>1200S</td><td class="matrix-cell matrix-pending" id="ref-1200S">○</td><td id="ref-1200S-exp">-</td><td id="ref-1200S-dec">-</td></tr>
                            <tr><td>1200L</td><td class="matrix-cell matrix-pending" id="ref-1200L">○</td><td id="ref-1200L-exp">-</td><td id="ref-1200L-dec">-</td></tr>
                            <tr><td>2400S</td><td class="matrix-cell matrix-pending" id="ref-2400S">○</td><td id="ref-2400S-exp">-</td><td id="ref-2400S-dec">-</td></tr>
                            <tr><td>2400L</td><td class="matrix-cell matrix-pending" id="ref-2400L">○</td><td id="ref-2400L-exp">-</td><td id="ref-2400L-dec">-</td></tr>
                        </tbody>
                    </table>
                </div>
                
                <div class="matrix-container" style="margin-top:20px;">
                    <h3 style="color:#00d4ff; margin:0 0 15px 0;">🚀 PhoenixNest Reference PCM Decode Test</h3>
                    <p style="color:#888; font-size:12px; margin-bottom:15px;">
                        Test PhoenixNest decoding against the same reference PCM files. 
                        Validates PhoenixNest RX chain is working correctly.
                    </p>
                    <div class="test-controls" style="margin-bottom:15px;">
                        <button class="btn-run" id="btn-pn-ref-pcm" onclick="runPnRefPcmTest()">
                            ▶ Test All Reference PCMs on PhoenixNest
                        </button>
                        <span id="pn-ref-pcm-progress" style="color:#aaa; margin-left:10px;">Progress: 0/12</span>
                    </div>
                    <table class="matrix-table">
                        <thead>
                            <tr>
                                <th>Mode</th>
                                <th>PhoenixNest Decode</th>
                                <th>Expected</th>
                                <th>Decoded</th>
                            </tr>
                        </thead>
                        <tbody id="pn-ref-pcm-body">
                            <tr><td>75S</td><td class="matrix-cell matrix-pending" id="pn-ref-75S">○</td><td id="pn-ref-75S-exp">-</td><td id="pn-ref-75S-dec">-</td></tr>
                            <tr><td>75L</td><td class="matrix-cell matrix-pending" id="pn-ref-75L">○</td><td id="pn-ref-75L-exp">-</td><td id="pn-ref-75L-dec">-</td></tr>
                            <tr><td>150S</td><td class="matrix-cell matrix-pending" id="pn-ref-150S">○</td><td id="pn-ref-150S-exp">-</td><td id="pn-ref-150S-dec">-</td></tr>
                            <tr><td>150L</td><td class="matrix-cell matrix-pending" id="pn-ref-150L">○</td><td id="pn-ref-150L-exp">-</td><td id="pn-ref-150L-dec">-</td></tr>
                            <tr><td>300S</td><td class="matrix-cell matrix-pending" id="pn-ref-300S">○</td><td id="pn-ref-300S-exp">-</td><td id="pn-ref-300S-dec">-</td></tr>
                            <tr><td>300L</td><td class="matrix-cell matrix-pending" id="pn-ref-300L">○</td><td id="pn-ref-300L-exp">-</td><td id="pn-ref-300L-dec">-</td></tr>
                            <tr><td>600S</td><td class="matrix-cell matrix-pending" id="pn-ref-600S">○</td><td id="pn-ref-600S-exp">-</td><td id="pn-ref-600S-dec">-</td></tr>
                            <tr><td>600L</td><td class="matrix-cell matrix-pending" id="pn-ref-600L">○</td><td id="pn-ref-600L-exp">-</td><td id="pn-ref-600L-dec">-</td></tr>
                            <tr><td>1200S</td><td class="matrix-cell matrix-pending" id="pn-ref-1200S">○</td><td id="pn-ref-1200S-exp">-</td><td id="pn-ref-1200S-dec">-</td></tr>
                            <tr><td>1200L</td><td class="matrix-cell matrix-pending" id="pn-ref-1200L">○</td><td id="pn-ref-1200L-exp">-</td><td id="pn-ref-1200L-dec">-</td></tr>
                            <tr><td>2400S</td><td class="matrix-cell matrix-pending" id="pn-ref-2400S">○</td><td id="pn-ref-2400S-exp">-</td><td id="pn-ref-2400S-dec">-</td></tr>
                            <tr><td>2400L</td><td class="matrix-cell matrix-pending" id="pn-ref-2400L">○</td><td id="pn-ref-2400L-exp">-</td><td id="pn-ref-2400L-dec">-</td></tr>
                        </tbody>
                    </table>
                </div>
                </div><!-- end subtab-reference -->
                
                <div class="interop-log" id="interop-log">
                    <div class="log-info">[INFO] MS-DMT Interop Test Log</div>
                    <div class="log-info">[INFO] Connect to MS-DMT to begin testing</div>
                </div>
            </div>
        </div><!-- end tab-interop -->
        
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
            if (tabName === 'melpe') {
                loadMelpeFiles();
                loadCustomRecordings();
            }
        }
        
        // Sub-tab switching for MS-DMT Interop
        function showSubTab(subTabName) {
            document.querySelectorAll('.sub-tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.sub-tab-content').forEach(t => t.classList.remove('active'));
            document.querySelector('.sub-tab[onclick*="' + subTabName + '"]').classList.add('active');
            document.getElementById('subtab-' + subTabName).classList.add('active');
        }
        
        // ============ MELPE VOCODER ============
        let audioContextInput = null;
        let audioContextOutput = null;
        let inputAudioBuffer = null;
        let outputAudioBuffer = null;
        let inputSourceNode = null;
        let outputSourceNode = null;
        let melpeOutputFile = '';
        
        function updateRateBadge() {
            const rate = document.getElementById('melpe-rate').value;
            const badge = document.getElementById('rate-badge');
            badge.textContent = rate + ' bps';
            badge.className = 'rate-badge rate-' + rate;
        }
        
        async function loadMelpeFiles() {
            const inputSelect = document.getElementById('melpe-input');
            const selectedFile = inputSelect.value;
            
            // Update file info
            document.getElementById('input-file-info').textContent = 'examples/melpe_test_audio/' + selectedFile;
            
            // Try to load the input audio for playback
            try {
                const response = await fetch('/melpe-audio?file=' + encodeURIComponent(selectedFile));
                if (response.ok) {
                    const arrayBuffer = await response.arrayBuffer();
                    
                    // Create audio context if not exists
                    if (!audioContextInput) {
                        audioContextInput = new (window.AudioContext || window.webkitAudioContext)();
                    }
                    
                    // Convert raw PCM to AudioBuffer (16-bit signed, 8kHz, mono)
                    const dataView = new DataView(arrayBuffer);
                    const numSamples = arrayBuffer.byteLength / 2;
                    inputAudioBuffer = audioContextInput.createBuffer(1, numSamples, 8000);
                    const channelData = inputAudioBuffer.getChannelData(0);
                    
                    for (let i = 0; i < numSamples; i++) {
                        const int16 = dataView.getInt16(i * 2, true); // little-endian
                        channelData[i] = int16 / 32768.0;
                    }
                    
                    document.getElementById('btn-play-input').disabled = false;
                    const duration = (numSamples / 8000).toFixed(1);
                    document.getElementById('input-file-info').textContent = 
                        'examples/melpe_test_audio/' + selectedFile + ' (' + duration + 's)';
                    
                    // Draw waveform
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
            
            // Set canvas size
            canvas.width = container.clientWidth || 300;
            canvas.height = container.clientHeight || 60;
            
            // Draw waveform
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
                    
                    // Load output audio
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
            
            if (audioContextInput.state === 'suspended') {
                audioContextInput.resume();
            }
            
            // Stop any existing playback
            if (inputSourceNode) {
                inputSourceNode.stop();
            }
            
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
            if (inputSourceNode) {
                inputSourceNode.stop();
                inputSourceNode = null;
            }
            document.getElementById('btn-play-input').style.display = '';
            document.getElementById('btn-stop-input').style.display = 'none';
        }
        
        function playOutputAudio() {
            if (!outputAudioBuffer) return;
            
            if (audioContextOutput.state === 'suspended') {
                audioContextOutput.resume();
            }
            
            if (outputSourceNode) {
                outputSourceNode.stop();
            }
            
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
            if (outputSourceNode) {
                outputSourceNode.stop();
                outputSourceNode = null;
            }
            document.getElementById('btn-play-output').style.display = '';
            document.getElementById('btn-stop-output').style.display = 'none';
        }
        
        // Update input when selection changes
        document.addEventListener('DOMContentLoaded', function() {
            document.getElementById('melpe-input').addEventListener('change', loadMelpeFiles);
        });
        
        // Handle file selection change
        function onFileSelectionChange() {
            loadMelpeFiles();
        }
        
        // ============ AUDIO RECORDING ============
        let mediaRecorder = null;
        let recordedChunks = [];
        let recordingStream = null;
        let recordedPcmData = null;
        let recordingStartTime = null;
        let recordingTimer = null;
        
        async function toggleRecording() {
            const btn = document.getElementById('btn-record');
            const status = document.getElementById('record-status');
            const timer = document.getElementById('record-timer');
            
            if (mediaRecorder && mediaRecorder.state === 'recording') {
                // Stop recording
                mediaRecorder.stop();
                btn.innerHTML = '🎤 Start Recording';
                btn.classList.remove('recording');
                status.textContent = 'Processing...';
                status.classList.remove('recording');
                timer.style.display = 'none';
                clearInterval(recordingTimer);
            } else {
                // Start recording
                try {
                    recordedChunks = [];
                    recordedPcmData = null;
                    
                    // Request microphone access
                    recordingStream = await navigator.mediaDevices.getUserMedia({ 
                        audio: { 
                            sampleRate: 48000,  // Browser will give us what it can
                            channelCount: 1,
                            echoCancellation: true,
                            noiseSuppression: true
                        } 
                    });
                    
                    mediaRecorder = new MediaRecorder(recordingStream, { mimeType: 'audio/webm' });
                    
                    mediaRecorder.ondataavailable = (e) => {
                        if (e.data.size > 0) {
                            recordedChunks.push(e.data);
                        }
                    };
                    
                    mediaRecorder.onstop = async () => {
                        // Stop all tracks
                        recordingStream.getTracks().forEach(track => track.stop());
                        
                        // Convert to 8kHz 16-bit PCM
                        status.textContent = 'Converting to 8kHz PCM...';
                        await convertRecordingToPcm();
                    };
                    
                    mediaRecorder.start(100);  // Collect data every 100ms
                    recordingStartTime = Date.now();
                    
                    btn.innerHTML = '⏹ Stop Recording';
                    btn.classList.add('recording');
                    status.textContent = 'Recording...';
                    status.classList.add('recording');
                    timer.style.display = 'inline';
                    timer.textContent = '00:00';
                    
                    // Update timer
                    recordingTimer = setInterval(() => {
                        const elapsed = Math.floor((Date.now() - recordingStartTime) / 1000);
                        const mins = Math.floor(elapsed / 60).toString().padStart(2, '0');
                        const secs = (elapsed % 60).toString().padStart(2, '0');
                        timer.textContent = mins + ':' + secs;
                    }, 1000);
                    
                    document.getElementById('btn-save-recording').disabled = true;
                    
                } catch (err) {
                    status.textContent = 'Error: ' + err.message;
                    console.error('Recording error:', err);
                }
            }
        }
        
        async function convertRecordingToPcm() {
            const status = document.getElementById('record-status');
            const saveBtn = document.getElementById('btn-save-recording');
            
            try {
                // Create blob from recorded chunks
                const blob = new Blob(recordedChunks, { type: 'audio/webm' });
                const arrayBuffer = await blob.arrayBuffer();
                
                // Decode using AudioContext
                const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
                const audioBuffer = await audioCtx.decodeAudioData(arrayBuffer);
                
                // Resample to 8kHz
                const offlineCtx = new OfflineAudioContext(1, 
                    Math.ceil(audioBuffer.duration * 8000), 8000);
                
                const source = offlineCtx.createBufferSource();
                source.buffer = audioBuffer;
                source.connect(offlineCtx.destination);
                source.start();
                
                const resampledBuffer = await offlineCtx.startRendering();
                const floatData = resampledBuffer.getChannelData(0);
                
                // Convert to 16-bit PCM
                recordedPcmData = new Int16Array(floatData.length);
                for (let i = 0; i < floatData.length; i++) {
                    const s = Math.max(-1, Math.min(1, floatData[i]));
                    recordedPcmData[i] = s < 0 ? s * 32768 : s * 32767;
                }
                
                const duration = (recordedPcmData.length / 8000).toFixed(1);
                status.textContent = 'Ready to save (' + duration + 's at 8kHz)';
                saveBtn.disabled = false;
                
                // Preview waveform in input viz
                drawWaveform('input-viz', floatData);
                
            } catch (err) {
                status.textContent = 'Conversion error: ' + err.message;
                console.error('Conversion error:', err);
            }
        }
        
        async function saveRecording() {
            if (!recordedPcmData) return;
            
            const nameInput = document.getElementById('record-name');
            const status = document.getElementById('record-status');
            const saveBtn = document.getElementById('btn-save-recording');
            
            // Generate filename
            let baseName = nameInput.value.trim() || 'recording';
            // Sanitize filename
            baseName = baseName.replace(/[^a-zA-Z0-9_-]/g, '_');
            const timestamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
            const filename = baseName + '_' + timestamp + '_8k.pcm';
            
            status.textContent = 'Saving...';
            saveBtn.disabled = true;
            
            try {
                // Convert Int16Array to base64
                const uint8 = new Uint8Array(recordedPcmData.buffer);
                let binary = '';
                for (let i = 0; i < uint8.length; i++) {
                    binary += String.fromCharCode(uint8[i]);
                }
                const base64Data = btoa(binary);
                
                // Send to server
                const response = await fetch('/melpe-save-recording', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        filename: filename,
                        pcm_data: base64Data
                    })
                });
                
                const result = await response.json();
                
                if (result.success) {
                    status.textContent = 'Saved: ' + filename;
                    
                    // Add to dropdown and select it
                    const select = document.getElementById('melpe-input');
                    const option = document.createElement('option');
                    option.value = filename;
                    option.textContent = '🎤 ' + baseName + ' (' + (recordedPcmData.length / 8000).toFixed(1) + 's)';
                    select.appendChild(option);
                    select.value = filename;
                    
                    // Load the new file
                    loadMelpeFiles();
                    
                    // Clear recorded data
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
        
        // Load custom recordings on tab load
        async function loadCustomRecordings() {
            try {
                const response = await fetch('/melpe-list-recordings');
                const result = await response.json();
                
                if (result.recordings && result.recordings.length > 0) {
                    const select = document.getElementById('melpe-input');
                    
                    // Add separator if there are custom recordings
                    const separator = document.createElement('option');
                    separator.disabled = true;
                    separator.textContent = '── Your Recordings ──';
                    select.appendChild(separator);
                    
                    // Add each custom recording
                    result.recordings.forEach(rec => {
                        const option = document.createElement('option');
                        option.value = rec.filename;
                        option.textContent = '🎤 ' + rec.name + ' (' + rec.duration + 's)';
                        select.appendChild(option);
                    });
                }
            } catch (err) {
                console.error('Failed to load recordings:', err);
            }
        }
        
        // ============ MS-DMT INTEROP ============
        let msdmtConnected = false;
        let interopTestRunning = false;
        
        function interopLog(message, type = 'info') {
            const log = document.getElementById('interop-log');
            const timestamp = new Date().toLocaleTimeString();
            const className = 'log-' + type;
            log.innerHTML += '<div class="' + className + '">[' + timestamp + '] ' + message + '</div>';
            log.scrollTop = log.scrollHeight;
        }
        
        let pnServerRunning = false;
        
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
        
        // Auto-check server status when interop tab is shown
        document.addEventListener('DOMContentLoaded', function() {
            const tabInterop = document.getElementById('tab-interop');
            if (tabInterop) {
                const observer = new MutationObserver(function(mutations) {
                    mutations.forEach(function(mutation) {
                        if (mutation.attributeName === 'class' && tabInterop.classList.contains('active')) {
                            checkPnServerStatus();
                        }
                    });
                });
                observer.observe(tabInterop, { attributes: true });
            }
        });
        
        async function toggleMsdmtConnection() {
            const btn = document.getElementById('btn-msdmt-connect');
            const dot = document.getElementById('msdmt-status-dot');
            const text = document.getElementById('msdmt-status-text');
            
            if (msdmtConnected) {
                // Disconnect
                try {
                    await fetch('/msdmt-disconnect');
                    msdmtConnected = false;
                    dot.className = 'status-dot disconnected';
                    text.textContent = 'Disconnected';
                    btn.textContent = 'Connect to MS-DMT';
                    btn.classList.remove('btn-disconnect');
                    document.getElementById('btn-test1').disabled = true;
                    document.getElementById('btn-test2').disabled = true;
                    document.getElementById('btn-matrix').disabled = true;
                    document.getElementById('btn-ref-pcm').disabled = true;
                    interopLog('Disconnected from MS-DMT', 'info');
                } catch (err) {
                    interopLog('Disconnect error: ' + err.message, 'error');
                }
            } else {
                // Connect
                const host = document.getElementById('msdmt-host').value;
                const ctrlPort = document.getElementById('msdmt-ctrl-port').value;
                const dataPort = document.getElementById('msdmt-data-port').value;
                
                dot.className = 'status-dot connecting';
                text.textContent = 'Connecting...';
                btn.disabled = true;
                interopLog('Connecting to MS-DMT at ' + host + ':' + ctrlPort + '/' + dataPort + '...', 'info');
                
                try {
                    const response = await fetch('/msdmt-connect?host=' + encodeURIComponent(host) + 
                        '&ctrl=' + ctrlPort + '&data=' + dataPort);
                    const result = await response.json();
                    
                    if (result.success) {
                        msdmtConnected = true;
                        dot.className = 'status-dot connected';
                        text.textContent = 'Connected - ' + (result.message || 'MODEM READY');
                        btn.textContent = 'Disconnect';
                        btn.classList.add('btn-disconnect');
                        document.getElementById('btn-test1').disabled = false;
                        document.getElementById('btn-test2').disabled = false;
                        document.getElementById('btn-matrix').disabled = false;
                        document.getElementById('btn-ref-pcm').disabled = false;
                        interopLog('Connected to MS-DMT: ' + result.message, 'rx');
                    } else {
                        dot.className = 'status-dot disconnected';
                        text.textContent = 'Connection failed: ' + result.message;
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
        
        function updateTestStep(testId, stepIndex, status) {
            const steps = document.getElementById(testId + '-steps').children;
            if (stepIndex < steps.length) {
                const icon = steps[stepIndex].querySelector('.step-icon');
                icon.className = 'step-icon step-' + status;
                if (status === 'pending') icon.textContent = '○';
                else if (status === 'running') icon.textContent = '●';
                else if (status === 'complete') icon.textContent = '✓';
                else if (status === 'error') icon.textContent = '✗';
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
        
        async function runInteropTest1() {
            if (!msdmtConnected || interopTestRunning) return;
            interopTestRunning = true;
            
            const mode = document.getElementById('interop-mode-1').value;
            const message = document.getElementById('interop-msg-1').value;
            const txDir = document.getElementById('msdmt-tx-dir').value;
            
            document.getElementById('btn-test1').disabled = true;
            resetTestSteps('test1', 8);
            interopLog('Starting Test 1: MS-DMT TX → PhoenixNest RX, Mode: ' + mode, 'info');
            
            try {
                const response = await fetch('/msdmt-test1?mode=' + encodeURIComponent(mode) + 
                    '&message=' + encodeURIComponent(message) + '&txdir=' + encodeURIComponent(txDir));
                
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
                                    updateTestStep('test1', data.step, data.status);
                                }
                                if (data.log) {
                                    interopLog(data.log, data.logType || 'info');
                                }
                                if (data.result) {
                                    const result = document.getElementById('test1-result');
                                    result.className = 'test-result ' + (data.success ? 'success' : 'failure');
                                    result.textContent = data.result;
                                    updateMatrixCell(mode, 1, data.success);
                                }
                            } catch (e) {}
                        }
                    }
                }
            } catch (err) {
                interopLog('Test 1 error: ' + err.message, 'error');
                document.getElementById('test1-result').className = 'test-result failure';
                document.getElementById('test1-result').textContent = 'Error: ' + err.message;
            }
            
            document.getElementById('btn-test1').disabled = false;
            interopTestRunning = false;
        }
        
        async function runInteropTest2() {
            if (!msdmtConnected || interopTestRunning) return;
            interopTestRunning = true;
            
            const mode = document.getElementById('interop-mode-2').value;
            const message = document.getElementById('interop-msg-2').value;
            
            document.getElementById('btn-test2').disabled = true;
            resetTestSteps('test2', 7);
            interopLog('Starting Test 2: PhoenixNest TX → MS-DMT RX, Mode: ' + mode, 'info');
            
            try {
                const response = await fetch('/msdmt-test2?mode=' + encodeURIComponent(mode) + 
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
                                    updateTestStep('test2', data.step, data.status);
                                }
                                if (data.log) {
                                    interopLog(data.log, data.logType || 'info');
                                }
                                if (data.result) {
                                    const result = document.getElementById('test2-result');
                                    result.className = 'test-result ' + (data.success ? 'success' : 'failure');
                                    result.textContent = data.result;
                                    updateMatrixCell(mode, 2, data.success);
                                }
                            } catch (e) {}
                        }
                    }
                }
            } catch (err) {
                interopLog('Test 2 error: ' + err.message, 'error');
                document.getElementById('test2-result').className = 'test-result failure';
                document.getElementById('test2-result').textContent = 'Error: ' + err.message;
            }
            
            document.getElementById('btn-test2').disabled = false;
            interopTestRunning = false;
        }
        
        function updateMatrixCell(mode, testNum, success) {
            const cell = document.getElementById('m-' + mode + '-' + testNum);
            if (cell) {
                cell.className = 'matrix-cell ' + (success ? 'matrix-pass' : 'matrix-fail');
                cell.textContent = success ? '✓' : '✗';
            }
        }
        
        let matrixTestAborted = false;
        
        function stopMatrixTest() {
            matrixTestAborted = true;
            interopLog('Matrix test aborted by user', 'error');
            document.getElementById('btn-matrix-stop').style.display = 'none';
        }
        
        async function runRefPcmTest() {
            if (!msdmtConnected || interopTestRunning) return;
            interopTestRunning = true;
            
            const modes = ['75S', '75L', '150S', '150L', '300S', '300L', 
                          '600S', '600L', '1200S', '1200L', '2400S', '2400L'];
            let completed = 0;
            let passed = 0;
            
            document.getElementById('btn-ref-pcm').disabled = true;
            document.getElementById('btn-matrix').disabled = true;
            document.getElementById('btn-test1').disabled = true;
            document.getElementById('btn-test2').disabled = true;
            
            // Reset all cells
            for (const mode of modes) {
                const cell = document.getElementById('ref-' + mode);
                cell.className = 'matrix-cell matrix-pending';
                cell.textContent = '○';
                document.getElementById('ref-' + mode + '-exp').textContent = '-';
                document.getElementById('ref-' + mode + '-dec').textContent = '-';
            }
            
            interopLog('Starting Reference PCM decode test (12 modes)', 'info');
            
            for (const mode of modes) {
                const cell = document.getElementById('ref-' + mode);
                cell.className = 'matrix-cell matrix-running';
                cell.textContent = '●';
                
                try {
                    const resp = await fetch('/msdmt-ref-pcm?mode=' + mode);
                    const result = await resp.json();
                    
                    document.getElementById('ref-' + mode + '-exp').textContent = result.expected || '-';
                    document.getElementById('ref-' + mode + '-dec').textContent = result.decoded || '0';
                    
                    if (result.success) {
                        cell.className = 'matrix-cell matrix-pass';
                        cell.textContent = '✓';
                        passed++;
                        interopLog(mode + ' Ref PCM: PASS (' + result.decoded + ' bytes)', 'rx');
                    } else {
                        cell.className = 'matrix-cell matrix-fail';
                        cell.textContent = '✗';
                        interopLog(mode + ' Ref PCM: FAIL - ' + (result.error || 'Unknown'), 'error');
                    }
                } catch (err) {
                    cell.className = 'matrix-cell matrix-fail';
                    cell.textContent = '✗';
                    interopLog(mode + ' Ref PCM: ERROR - ' + err.message, 'error');
                }
                
                completed++;
                document.getElementById('ref-pcm-progress').textContent = 'Progress: ' + completed + '/12';
            }
            
            interopLog('Reference PCM test complete: ' + passed + '/12 passed', passed === 12 ? 'rx' : 'error');
            
            interopTestRunning = false;
            document.getElementById('btn-ref-pcm').disabled = false;
            document.getElementById('btn-matrix').disabled = false;
            document.getElementById('btn-test1').disabled = false;
            document.getElementById('btn-test2').disabled = false;
        }
        
        // PhoenixNest Reference PCM test
        async function runPnRefPcmTest() {
            if (interopTestRunning) return;
            
            // Check if PhoenixNest server is running
            if (!pnServerRunning) {
                interopLog('PhoenixNest server not running - start it first in Connection Setup', 'error');
                alert('Please start PhoenixNest server first in the Connection Setup tab');
                return;
            }
            
            interopTestRunning = true;
            
            const modes = ['75S', '75L', '150S', '150L', '300S', '300L', 
                          '600S', '600L', '1200S', '1200L', '2400S', '2400L'];
            let completed = 0;
            let passed = 0;
            
            document.getElementById('btn-pn-ref-pcm').disabled = true;
            
            // Reset all cells
            for (const mode of modes) {
                const cell = document.getElementById('pn-ref-' + mode);
                cell.className = 'matrix-cell matrix-pending';
                cell.textContent = '○';
                document.getElementById('pn-ref-' + mode + '-exp').textContent = '-';
                document.getElementById('pn-ref-' + mode + '-dec').textContent = '-';
            }
            
            interopLog('Starting PhoenixNest Reference PCM decode test (12 modes)', 'info');
            
            for (const mode of modes) {
                const cell = document.getElementById('pn-ref-' + mode);
                cell.className = 'matrix-cell matrix-running';
                cell.textContent = '●';
                
                try {
                    const resp = await fetch('/pn-ref-pcm?mode=' + mode);
                    const result = await resp.json();
                    
                    document.getElementById('pn-ref-' + mode + '-exp').textContent = result.expected || '-';
                    document.getElementById('pn-ref-' + mode + '-dec').textContent = result.decoded || '0';
                    
                    if (result.success) {
                        cell.className = 'matrix-cell matrix-pass';
                        cell.textContent = '✓';
                        passed++;
                        interopLog(mode + ' PN Ref PCM: PASS (' + result.decoded + ' bytes)', 'rx');
                    } else {
                        cell.className = 'matrix-cell matrix-fail';
                        cell.textContent = '✗';
                        interopLog(mode + ' PN Ref PCM: FAIL - ' + (result.error || 'Unknown'), 'error');
                    }
                } catch (err) {
                    cell.className = 'matrix-cell matrix-fail';
                    cell.textContent = '✗';
                    interopLog(mode + ' PN Ref PCM: ERROR - ' + err.message, 'error');
                }
                
                completed++;
                document.getElementById('pn-ref-pcm-progress').textContent = 'Progress: ' + completed + '/12';
            }
            
            interopLog('PhoenixNest Reference PCM test complete: ' + passed + '/12 passed', passed === 12 ? 'rx' : 'error');
            
            interopTestRunning = false;
            document.getElementById('btn-pn-ref-pcm').disabled = false;
        }
        
        async function runFullMatrix() {
            if (!msdmtConnected || interopTestRunning) return;
            interopTestRunning = true;
            matrixTestAborted = false;
            
            const modes = ['75S', '75L', '150S', '150L', '300S', '300L', 
                          '600S', '600L', '1200S', '1200L', '2400S', '2400L'];
            const message = 'INTEROP TEST MESSAGE';
            const txDir = document.getElementById('msdmt-tx-dir').value;
            let completed = 0;
            const total = modes.length * 2;
            
            // Results tracking for report
            const results = {
                msdmtToPn: {},  // mode -> {success, decoded, expected, error}
                pnToMsdmt: {}   // mode -> {success, decoded, expected, error}
            };
            const startTime = Date.now();
            
            document.getElementById('btn-matrix').disabled = true;
            document.getElementById('btn-matrix-stop').style.display = 'inline-block';
            document.getElementById('btn-test1').disabled = true;
            document.getElementById('btn-test2').disabled = true;
            
            // Reset all matrix cells
            for (const mode of modes) {
                for (let t = 1; t <= 2; t++) {
                    const cell = document.getElementById('m-' + mode + '-' + t);
                    cell.className = 'matrix-cell matrix-pending';
                    cell.textContent = '○';
                }
            }
            
            interopLog('Starting full compatibility matrix test (24 tests)', 'info');
            
            for (const mode of modes) {
                // Check for abort
                if (matrixTestAborted) {
                    interopLog('Matrix test aborted', 'error');
                    break;
                }
                
                // Test 1: MS-DMT TX → PN RX
                const cell1 = document.getElementById('m-' + mode + '-1');
                cell1.className = 'matrix-cell matrix-running';
                cell1.textContent = '●';
                
                try {
                    const resp1 = await fetch('/msdmt-test1-quick?mode=' + mode + 
                        '&message=' + encodeURIComponent(message) + '&txdir=' + encodeURIComponent(txDir));
                    const result1 = await resp1.json();
                    results.msdmtToPn[mode] = {
                        success: result1.success,
                        decoded: result1.decoded || 0,
                        expected: message.length,
                        modeDetected: result1.modeDetected || '',
                        error: result1.error || ''
                    };
                    updateMatrixCell(mode, 1, result1.success);
                    interopLog(mode + ' MS-DMT→PN: ' + (result1.success ? 'PASS (' + result1.decoded + ' bytes)' : 'FAIL - ' + (result1.error || 'No data')), 
                              result1.success ? 'rx' : 'error');
                } catch (err) {
                    results.msdmtToPn[mode] = { success: false, decoded: 0, expected: message.length, error: err.message };
                    updateMatrixCell(mode, 1, false);
                    interopLog(mode + ' MS-DMT→PN: ERROR - ' + err.message, 'error');
                }
                completed++;
                document.getElementById('matrix-progress').textContent = 'Progress: ' + completed + '/' + total;
                
                // Test 2: PN TX → MS-DMT RX
                const cell2 = document.getElementById('m-' + mode + '-2');
                cell2.className = 'matrix-cell matrix-running';
                cell2.textContent = '●';
                
                try {
                    const resp2 = await fetch('/msdmt-test2-quick?mode=' + mode + 
                        '&message=' + encodeURIComponent(message));
                    const result2 = await resp2.json();
                    results.pnToMsdmt[mode] = {
                        success: result2.success,
                        decoded: result2.decoded || 0,
                        expected: message.length,
                        modeDetected: result2.modeDetected || '',
                        error: result2.error || ''
                    };
                    updateMatrixCell(mode, 2, result2.success);
                    interopLog(mode + ' PN→MS-DMT: ' + (result2.success ? 'PASS (' + result2.decoded + ' bytes)' : 'FAIL - ' + (result2.error || 'No data')), 
                              result2.success ? 'rx' : 'error');
                } catch (err) {
                    results.pnToMsdmt[mode] = { success: false, decoded: 0, expected: message.length, error: err.message };
                    updateMatrixCell(mode, 2, false);
                    interopLog(mode + ' PN→MS-DMT: ERROR - ' + err.message, 'error');
                }
                completed++;
                document.getElementById('matrix-progress').textContent = 'Progress: ' + completed + '/' + total;
            }
            
            // Hide stop button
            document.getElementById('btn-matrix-stop').style.display = 'none';
            
            // Calculate summary stats
            const endTime = Date.now();
            const duration = Math.round((endTime - startTime) / 1000);
            let msdmtToPnPass = 0, pnToMsdmtPass = 0;
            for (const mode of modes) {
                if (results.msdmtToPn[mode]?.success) msdmtToPnPass++;
                if (results.pnToMsdmt[mode]?.success) pnToMsdmtPass++;
            }
            
            interopLog('Full matrix test complete: MS-DMT→PN ' + msdmtToPnPass + '/12, PN→MS-DMT ' + pnToMsdmtPass + '/12', 'info');
            
            // Generate markdown report
            const now = new Date();
            const dateStr = now.toLocaleDateString('en-US', { year: 'numeric', month: 'long', day: '2-digit' });
            const timeStr = now.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
            const durationStr = duration + ' seconds';
            const msdmtHost = document.getElementById('msdmt-host').value;
            const totalPass = msdmtToPnPass + pnToMsdmtPass;
            const overallRate = ((totalPass / total) * 100).toFixed(1);
            const rating = totalPass === total ? 'EXCELLENT' : totalPass >= total * 0.8 ? 'GOOD' : totalPass >= total * 0.5 ? 'FAIR' : 'POOR';
            
            let report = '# M110A Interop Test Report\\n\\n';
            report += '## Test Information\\n';
            report += '| Field | Value |\\n';
            report += '|-------|-------|\\n';
            report += '| **Test Type** | Interoperability |\\n';
            report += '| **MS-DMT Host** | ' + msdmtHost + ' |\\n';
            report += '| **Test Date** | ' + dateStr + ' ' + timeStr + ' |\\n';
            report += '| **Duration** | ' + durationStr + ' |\\n';
            report += '| **Test Message** | ' + message + ' |\\n';
            report += '| **Total Tests** | ' + total + ' |\\n';
            report += '| **Rating** | ' + rating + ' |\\n\\n';
            report += '---\\n\\n';
            
            report += '## Summary\\n\\n';
            report += '| Metric | Value |\\n';
            report += '|--------|-------|\\n';
            report += '| **Overall Pass Rate** | ' + overallRate + '% |\\n';
            report += '| **MS-DMT TX → PhoenixNest RX** | ' + msdmtToPnPass + '/12 |\\n';
            report += '| **PhoenixNest TX → MS-DMT RX** | ' + pnToMsdmtPass + '/12 |\\n';
            report += '| **Total Passed** | ' + totalPass + ' |\\n';
            report += '| **Total Failed** | ' + (total - totalPass) + ' |\\n\\n';
            report += '---\\n\\n';
            
            report += '## Compatibility Matrix\\n\\n';
            report += '| Mode | MS-DMT → PN | PN → MS-DMT | Details |\\n';
            report += '|------|:-----------:|:-----------:|---------|\\n';
            for (const mode of modes) {
                const r1 = results.msdmtToPn[mode] || {};
                const r2 = results.pnToMsdmt[mode] || {};
                const s1 = r1.success ? '✅ PASS' : '❌ FAIL';
                const s2 = r2.success ? '✅ PASS' : '❌ FAIL';
                let details = '';
                if (!r1.success && r1.error) details += 'MS-DMT→PN: ' + r1.error + ' ';
                if (!r2.success && r2.error) details += 'PN→MS-DMT: ' + r2.error;
                if (r1.success) details += r1.decoded + 'B decoded ';
                report += '| ' + mode + ' | ' + s1 + ' | ' + s2 + ' | ' + details.trim() + ' |\\n';
            }
            report += '\\n---\\n\\n';
            
            report += '## Detailed Results\\n\\n';
            report += '### MS-DMT TX → PhoenixNest RX\\n\\n';
            report += '| Mode | Status | Decoded | Expected | Mode Detected | Error |\\n';
            report += '|------|--------|---------|----------|---------------|-------|\\n';
            for (const mode of modes) {
                const r = results.msdmtToPn[mode] || {};
                report += '| ' + mode + ' | ' + (r.success ? 'PASS' : 'FAIL') + ' | ' + (r.decoded || 0) + ' | ' + (r.expected || 0) + ' | ' + (r.modeDetected || 'N/A') + ' | ' + (r.error || '-') + ' |\\n';
            }
            report += '\\n### PhoenixNest TX → MS-DMT RX\\n\\n';
            report += '| Mode | Status | Decoded | Expected | Mode Detected | Error |\\n';
            report += '|------|--------|---------|----------|---------------|-------|\\n';
            for (const mode of modes) {
                const r = results.pnToMsdmt[mode] || {};
                report += '| ' + mode + ' | ' + (r.success ? 'PASS' : 'FAIL') + ' | ' + (r.decoded || 0) + ' | ' + (r.expected || 0) + ' | ' + (r.modeDetected || 'N/A') + ' | ' + (r.error || '-') + ' |\\n';
            }
            
            // Save report
            interopLog('Saving interop report...', 'info');
            try {
                const saveResp = await fetch('/save-interop-report', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ content: report })
                });
                const saveResult = await saveResp.json();
                if (saveResult.success) {
                    interopLog('Report saved: ' + saveResult.filename, 'rx');
                } else {
                    interopLog('Failed to save report: ' + saveResult.message, 'error');
                }
            } catch (err) {
                interopLog('Error saving report: ' + err.message, 'error');
            }
            
            document.getElementById('btn-matrix').disabled = false;
            document.getElementById('btn-test1').disabled = false;
            document.getElementById('btn-test2').disabled = false;
            interopTestRunning = false;
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
    
    // MS-DMT connection state
    SOCKET msdmt_ctrl_sock_ = INVALID_SOCKET;
    SOCKET msdmt_data_sock_ = INVALID_SOCKET;
    std::string msdmt_host_;
    int msdmt_ctrl_port_ = 4999;
    int msdmt_data_port_ = 4998;
    bool msdmt_connected_ = false;
    
    // PhoenixNest server connection state
    SOCKET pn_ctrl_sock_ = INVALID_SOCKET;
    SOCKET pn_data_sock_ = INVALID_SOCKET;
    std::string pn_host_ = "127.0.0.1";
    bool pn_connected_ = false;
    
    void handle_client(SOCKET client) {
        // Initial buffer for headers
        char header_buf[8192];
        int n = recv(client, header_buf, sizeof(header_buf) - 1, 0);
        if (n <= 0) {
            closesocket(client);
            return;
        }
        header_buf[n] = '\0';
        
        std::string request(header_buf, n);
        
        // Check if we need to read more data (for POST requests with Content-Length)
        size_t content_length = 0;
        size_t cl_pos = request.find("Content-Length:");
        if (cl_pos != std::string::npos) {
            size_t cl_end = request.find("\r\n", cl_pos);
            std::string cl_str = request.substr(cl_pos + 15, cl_end - cl_pos - 15);
            // Trim whitespace
            while (!cl_str.empty() && (cl_str[0] == ' ' || cl_str[0] == '\t')) {
                cl_str = cl_str.substr(1);
            }
            content_length = std::stoul(cl_str);
        }
        
        // Find where body starts
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != std::string::npos && content_length > 0) {
            body_start += 4;
            size_t body_received = request.size() - body_start;
            
            // Read remaining body data if needed
            while (body_received < content_length) {
                char chunk[65536];
                size_t to_read = std::min(sizeof(chunk), content_length - body_received);
                int chunk_n = recv(client, chunk, (int)to_read, 0);
                if (chunk_n <= 0) break;
                request.append(chunk, chunk_n);
                body_received += chunk_n;
            }
        }
        
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
        } else if (path.find("/melpe-audio?") == 0) {
            handle_melpe_audio(client, path);
        } else if (path.find("/melpe-run?") == 0) {
            handle_melpe_run(client, path);
        } else if (path.find("/melpe-output?") == 0) {
            handle_melpe_output(client, path);
        } else if (path == "/melpe-list-recordings") {
            handle_melpe_recordings(client);
        } else if (path == "/melpe-save-recording" && method == "POST") {
            handle_melpe_save_recording(client, request);
        } else if (path.find("/pn-server-start?") == 0) {
            handle_pn_server_start(client, path);
        } else if (path == "/pn-server-stop") {
            handle_pn_server_stop(client);
        } else if (path == "/pn-server-status") {
            handle_pn_server_status(client);
        } else if (path == "/pn-connect") {
            handle_pn_connect(client);
        } else if (path == "/pn-disconnect") {
            handle_pn_disconnect(client);
        } else if (path.find("/msdmt-connect?") == 0) {
            handle_msdmt_connect(client, path);
        } else if (path == "/msdmt-disconnect") {
            handle_msdmt_disconnect(client);
        } else if (path.find("/msdmt-test1?") == 0) {
            handle_msdmt_test1(client, path);
        } else if (path.find("/msdmt-test2?") == 0) {
            handle_msdmt_test2(client, path);
        } else if (path.find("/msdmt-test1-quick?") == 0) {
            handle_msdmt_test1_quick(client, path);
        } else if (path.find("/msdmt-test2-quick?") == 0) {
            handle_msdmt_test2_quick(client, path);
        } else if (path.find("/msdmt-ref-pcm?") == 0) {
            handle_msdmt_ref_pcm(client, path);
        } else if (path.find("/pn-ref-pcm?") == 0) {
            handle_pn_ref_pcm(client, path);
        } else if (path == "/save-interop-report" && method == "POST") {
            handle_save_interop_report(client, request);
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
                if (filename.find("interop") != std::string::npos) type = "interop";
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
    
    // Find melpe test audio directory - works in both dev and deployed scenarios
    std::string find_melpe_audio_dir() {
        // Try multiple possible locations
        std::vector<std::string> candidates = {
            exe_dir_ + "examples/melpe_test_audio/",           // Same dir as exe (flat release)
            exe_dir_ + "../examples/melpe_test_audio/",        // Parent dir (bin/ subfolder release)
            exe_dir_ + "../../src/melpe_core/test_audio/",     // Dev: test/ -> src/melpe_core/test_audio/
            exe_dir_ + "../src/melpe_core/test_audio/",        // Dev: from project root
        };
        
        for (const auto& dir : candidates) {
            if (fs::exists(dir) && fs::is_directory(dir)) {
                // Return canonical (absolute, normalized) path with trailing separator
                try {
                    fs::path p = fs::canonical(dir);
                    std::string result = p.string();
                    // Ensure trailing separator
                    if (!result.empty() && result.back() != '\\' && result.back() != '/') {
                        result += fs::path::preferred_separator;
                    }
                    return result;
                } catch (...) {
                    return dir;  // Fallback if canonical fails
                }
            }
        }
        return "";  // Not found
    }
    
    // Find melpe_vocoder.exe - works in both dev and deployed scenarios
    std::string find_melpe_exe() {
        std::vector<std::string> candidates = {
            exe_dir_ + "melpe_vocoder.exe",                      // Same dir as test_gui
            exe_dir_ + "../bin/melpe_vocoder.exe",               // Sibling bin/ folder
            exe_dir_ + "../src/melpe_core/build/melpe_vocoder.exe",  // Dev build location
        };
        for (const auto& candidate : candidates) {
            if (fs::exists(candidate)) {
                try {
                    return fs::canonical(candidate).string();
                } catch (...) {
                    return candidate;
                }
            }
        }
        return "";
    }
    
    // Get canonical exe directory path
    std::string get_canonical_exe_dir() {
        try {
            fs::path p = fs::canonical(exe_dir_);
            std::string result = p.string();
            if (!result.empty() && result.back() != '\\' && result.back() != '/') {
                result += fs::path::preferred_separator;
            }
            return result;
        } catch (...) {
            return exe_dir_;
        }
    }
    
    void handle_melpe_audio(SOCKET client, const std::string& path) {
        // Parse filename from query string
        std::string filename;
        size_t pos = path.find("file=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            filename = url_decode(path.substr(pos + 5, end - pos - 5));
        }
        
        // Validate filename (no path traversal)
        if (filename.empty() || filename.find("..") != std::string::npos || 
            filename.find("/") != std::string::npos || filename.find("\\") != std::string::npos) {
            send_404(client);
            return;
        }
        
        // Find the melpe audio directory
        std::string audio_dir = find_melpe_audio_dir();
        if (audio_dir.empty()) {
            send_404(client);
            return;
        }
        
        // Try main audio directory first
        std::string filepath = audio_dir + filename;
        std::ifstream file(filepath, std::ios::binary);
        
        // If not found, try recordings subdirectory
        if (!file.is_open()) {
            filepath = audio_dir + "recordings" + PATH_SEP + filename;
            file.open(filepath, std::ios::binary);
        }
        
        if (!file.is_open()) {
            send_404(client);
            return;
        }
        
        // Read file content
        std::vector<char> content((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/octet-stream\r\n"
                 << "Content-Length: " << content.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n";
        std::string headers = response.str();
        send(client, headers.c_str(), (int)headers.size(), 0);
        send(client, content.data(), (int)content.size(), 0);
    }
    
    void handle_melpe_run(SOCKET client, const std::string& path) {
        // Parse parameters from query string
        std::string input_file, rate = "2400";
        
        size_t pos = path.find("input=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            input_file = url_decode(path.substr(pos + 6, end - pos - 6));
        }
        
        pos = path.find("rate=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            rate = path.substr(pos + 5, end - pos - 5);
        }
        
        // Validate
        if (input_file.empty() || input_file.find("..") != std::string::npos) {
            std::string error = "{\"success\":false,\"message\":\"Invalid input file\"}";
            send_json(client, error);
            return;
        }
        
        if (rate != "600" && rate != "1200" && rate != "2400") {
            std::string error = "{\"success\":false,\"message\":\"Invalid rate. Use 600, 1200, or 2400\"}";
            send_json(client, error);
            return;
        }
        
        // Find audio directory and melpe_vocoder.exe
        std::string audio_dir = find_melpe_audio_dir();
        if (audio_dir.empty()) {
            std::string error = "{\"success\":false,\"message\":\"MELPe test audio directory not found\"}";
            send_json(client, error);
            return;
        }
        
        // Find melpe_vocoder.exe
        std::string melpe_exe = find_melpe_exe();
        if (melpe_exe.empty()) {
            std::string error = "{\"success\":false,\"message\":\"melpe_vocoder.exe not found\"}";
            send_json(client, error);
            return;
        }
        
        // Create output filename in exe directory (use canonical path)
        std::string output_file = "melpe_output_" + rate + "bps.raw";
        std::string input_path = audio_dir + input_file;
        
        // If not found in main dir, try recordings subdirectory
        if (!fs::exists(input_path)) {
            input_path = audio_dir + "recordings" + PATH_SEP + input_file;
        }
        
        std::string output_path = get_canonical_exe_dir() + output_file;
        
        // On Windows, paths from fs::canonical already use backslashes
        // No additional normalization needed
        
        // Check if input exists
        if (!fs::exists(input_path)) {
            std::string error = "{\"success\":false,\"message\":\"Input file not found: " + input_file + "\"}";
            send_json(client, error);
            return;
        }
        
        // Build command - run melpe_vocoder.exe in loopback mode
        // Use cmd /c to properly handle paths with spaces
        std::string cmd = "cmd /c \"\"" + melpe_exe + "\" -r " + rate + 
                          " -m C -i \"" + input_path + "\" -o \"" + output_path + "\"\" 2>&1";
        
        // Execute vocoder
#ifdef _WIN32
        FILE* proc = _popen(cmd.c_str(), "r");
#else
        FILE* proc = popen(cmd.c_str(), "r");
#endif
        
        std::string output;
        if (proc) {
            char buf[256];
            while (fgets(buf, sizeof(buf), proc)) {
                output += buf;
            }
#ifdef _WIN32
            int ret = _pclose(proc);
#else
            int ret = pclose(proc);
#endif
            
            // Check if output file was created
            if (fs::exists(output_path)) {
                auto input_size = fs::file_size(input_path);
                auto output_size = fs::file_size(output_path);
                std::ostringstream json;
                json << "{\"success\":true,\"message\":\"Processed " 
                     << (input_size / 2 / 8000.0) << "s of audio at " << rate << " bps\""
                     << ",\"output_file\":\"" << output_file << "\""
                     << ",\"input_size\":" << input_size
                     << ",\"output_size\":" << output_size
                     << "}";
                send_json(client, json.str());
            } else {
                // Escape output for JSON
                std::string escaped;
                for (char c : output) {
                    if (c == '"') escaped += "\\\"";
                    else if (c == '\\') escaped += "\\\\";
                    else if (c == '\n') escaped += "\\n";
                    else if (c == '\r') continue;
                    else escaped += c;
                }
                std::string error = "{\"success\":false,\"message\":\"Vocoder failed: " + escaped + "\"}";
                send_json(client, error);
            }
        } else {
            std::string error = "{\"success\":false,\"message\":\"Could not start melpe_vocoder.exe\"}";
            send_json(client, error);
        }
    }
    
    void handle_melpe_output(SOCKET client, const std::string& path) {
        // Parse filename from query string
        std::string filename;
        size_t pos = path.find("file=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            filename = url_decode(path.substr(pos + 5, end - pos - 5));
        }
        
        // Validate filename (must be our output file pattern)
        if (filename.empty() || filename.find("..") != std::string::npos ||
            filename.find("/") != std::string::npos || filename.find("\\") != std::string::npos ||
            filename.find("melpe_output_") != 0) {
            send_404(client);
            return;
        }
        
        std::string filepath = exe_dir_ + filename;
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            send_404(client);
            return;
        }
        
        std::vector<char> content((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/octet-stream\r\n"
                 << "Content-Length: " << content.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n";
        std::string headers = response.str();
        send(client, headers.c_str(), (int)headers.size(), 0);
        send(client, content.data(), (int)content.size(), 0);
    }
    
    std::string get_recordings_dir() {
        // Create recordings directory in the melpe_test_audio folder
        std::string audio_dir = find_melpe_audio_dir();
        if (audio_dir.empty()) {
            // Fall back to exe_dir
            audio_dir = exe_dir_;
        }
        std::string rec_dir = audio_dir + "recordings" + PATH_SEP;
        
        // Create directory if it doesn't exist
        #ifdef _WIN32
        _mkdir(rec_dir.c_str());
        #else
        mkdir(rec_dir.c_str(), 0755);
        #endif
        
        return rec_dir;
    }
    
    void handle_melpe_recordings(SOCKET client) {
        std::string rec_dir = get_recordings_dir();
        
        std::ostringstream json;
        json << "{\"recordings\":[";
        
        bool first = true;
        #ifdef _WIN32
        WIN32_FIND_DATAA findData;
        std::string search = rec_dir + "*.pcm";
        HANDLE hFind = FindFirstFileA(search.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    if (!first) json << ",";
                    first = false;
                    std::string fname = findData.cFileName;
                    
                    // Get file size to calculate duration
                    std::string filepath = rec_dir + fname;
                    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
                    size_t filesize = 0;
                    if (file.is_open()) {
                        filesize = file.tellg();
                    }
                    double duration = (filesize / 2) / 8000.0;  // 16-bit samples at 8kHz
                    
                    // Extract base name (remove timestamp and extension)
                    std::string name = fname;
                    if (name.size() > 4) name = name.substr(0, name.size() - 4);  // remove .pcm
                    size_t underscore = name.rfind('_');
                    if (underscore != std::string::npos && underscore > 0) {
                        name = name.substr(0, underscore);  // remove _8k suffix
                    }
                    
                    json << "{\"filename\":\"" << fname << "\","
                         << "\"name\":\"" << name << "\","
                         << "\"duration\":" << std::fixed << std::setprecision(1) << duration << "}";
                }
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
        }
        #else
        DIR* dir = opendir(rec_dir.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string fname = entry->d_name;
                if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".pcm") {
                    if (!first) json << ",";
                    first = false;
                    
                    // Get file size to calculate duration
                    std::string filepath = rec_dir + fname;
                    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
                    size_t filesize = 0;
                    if (file.is_open()) {
                        filesize = file.tellg();
                    }
                    double duration = (filesize / 2) / 8000.0;
                    
                    // Extract base name
                    std::string name = fname;
                    if (name.size() > 4) name = name.substr(0, name.size() - 4);
                    size_t underscore = name.rfind('_');
                    if (underscore != std::string::npos && underscore > 0) {
                        name = name.substr(0, underscore);
                    }
                    
                    json << "{\"filename\":\"" << fname << "\","
                         << "\"name\":\"" << name << "\","
                         << "\"duration\":" << std::fixed << std::setprecision(1) << duration << "}";
                }
            }
            closedir(dir);
        }
        #endif
        
        json << "]}";
        send_json(client, json.str());
    }
    
    // Simple base64 decoder
    std::vector<unsigned char> base64_decode(const std::string& encoded) {
        static const std::string base64_chars = 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        std::vector<unsigned char> result;
        int val = 0, valb = -8;
        for (unsigned char c : encoded) {
            if (c == '=') break;
            size_t idx = base64_chars.find(c);
            if (idx == std::string::npos) continue;
            val = (val << 6) + (int)idx;
            valb += 6;
            if (valb >= 0) {
                result.push_back((unsigned char)((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return result;
    }
    
    void handle_melpe_save_recording(SOCKET client, const std::string& request) {
        // Find the body (after headers)
        size_t body_start = request.find("\r\n\r\n");
        if (body_start == std::string::npos) {
            send_json(client, "{\"success\":false,\"message\":\"Invalid request\"}");
            return;
        }
        
        std::string body = request.substr(body_start + 4);
        
        // Parse JSON (simple parsing)
        std::string filename;
        std::string pcm_data_b64;
        
        // Extract filename
        size_t fn_pos = body.find("\"filename\"");
        if (fn_pos != std::string::npos) {
            size_t colon = body.find(':', fn_pos);
            size_t quote1 = body.find('"', colon);
            size_t quote2 = body.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                filename = body.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
        
        // Extract pcm_data (base64)
        size_t pcm_pos = body.find("\"pcm_data\"");
        if (pcm_pos != std::string::npos) {
            size_t colon = body.find(':', pcm_pos);
            size_t quote1 = body.find('"', colon);
            size_t quote2 = body.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                pcm_data_b64 = body.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
        
        // Validate filename
        if (filename.empty()) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm* tm_info = std::localtime(&time);
            char buf[64];
            std::strftime(buf, sizeof(buf), "recording_%Y%m%d_%H%M%S.pcm", tm_info);
            filename = buf;
        }
        
        // Security: only allow safe filename characters
        for (char c : filename) {
            if (!isalnum(c) && c != '_' && c != '-' && c != '.') {
                send_json(client, "{\"success\":false,\"message\":\"Invalid filename characters\"}");
                return;
            }
        }
        
        // Ensure .pcm extension
        if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".pcm") {
            filename += ".pcm";
        }
        
        // Decode base64 PCM data
        std::vector<unsigned char> pcm_data = base64_decode(pcm_data_b64);
        
        if (pcm_data.empty()) {
            send_json(client, "{\"success\":false,\"message\":\"No audio data received\"}");
            return;
        }
        
        // Save to recordings directory
        std::string rec_dir = get_recordings_dir();
        std::string filepath = rec_dir + filename;
        
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            send_json(client, "{\"success\":false,\"message\":\"Failed to create file\"}");
            return;
        }
        
        file.write(reinterpret_cast<const char*>(pcm_data.data()), pcm_data.size());
        file.close();
        
        // Calculate duration for response
        double duration = (pcm_data.size() / 2) / 8000.0;  // 16-bit samples at 8kHz
        
        std::ostringstream json;
        json << "{\"success\":true,\"filename\":\"" << filename << "\",\"size\":" << pcm_data.size() 
             << ",\"duration\":" << std::fixed << std::setprecision(1) << duration << "}";
        send_json(client, json.str());
    }
    
    // ============ PHOENIXNEST SERVER CONTROL ============
    
    void handle_pn_server_start(SOCKET client, const std::string& path) {
        // If already running, return success with current PID
        if (pn_server_running_ && pn_server_pid_ != 0) {
#ifdef _WIN32
            // Check if process still exists
            DWORD exitCode;
            if (GetExitCodeProcess(pn_server_process_, &exitCode) && exitCode == STILL_ACTIVE) {
                std::ostringstream json;
                json << "{\"success\":true,\"pid\":" << pn_server_pid_ << ",\"message\":\"Already running\"}";
                send_json(client, json.str());
                return;
            } else {
                // Process exited, reset state
                CloseHandle(pn_server_process_);
                pn_server_process_ = NULL;
                pn_server_pid_ = 0;
                pn_server_running_ = false;
            }
#endif
        }
        
        // Parse ports from query string
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
        
        // Find server executable - check multiple locations
        std::vector<std::string> server_paths = {
            exe_dir_ + PATH_SEP + "m110a_server.exe",
            exe_dir_ + PATH_SEP + ".." + PATH_SEP + "server" + PATH_SEP + "m110a_server.exe",
            exe_dir_ + PATH_SEP + ".." + PATH_SEP + "release" + PATH_SEP + "bin" + PATH_SEP + "m110a_server.exe",
            "server" + std::string(PATH_SEP) + "m110a_server.exe",
            "release" + std::string(PATH_SEP) + "bin" + std::string(PATH_SEP) + "m110a_server.exe"
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
        // Build command line with ports
        std::string cmd = "\"" + server_exe + "\" --control-port " + std::to_string(ctrl_port) + 
                          " --data-port " + std::to_string(data_port);
        
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        
        // Create process detached
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;  // Hide console window
        
        char cmdLine[1024];
        strncpy(cmdLine, cmd.c_str(), sizeof(cmdLine) - 1);
        cmdLine[sizeof(cmdLine) - 1] = '\0';
        
        if (CreateProcessA(
            NULL,           // Application name
            cmdLine,        // Command line
            NULL,           // Process attributes
            NULL,           // Thread attributes
            FALSE,          // Inherit handles
            CREATE_NEW_CONSOLE | CREATE_NO_WINDOW,  // Creation flags
            NULL,           // Environment
            NULL,           // Current directory
            &si,            // Startup info
            &pi             // Process info
        )) {
            pn_server_process_ = pi.hProcess;
            pn_server_pid_ = pi.dwProcessId;
            pn_server_running_ = true;
            CloseHandle(pi.hThread);  // Don't need thread handle
            
            // Wait a moment for server to start
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
        // First try graceful termination
        if (pn_server_process_ != NULL) {
            TerminateProcess(pn_server_process_, 0);
            WaitForSingleObject(pn_server_process_, 3000);  // Wait up to 3 seconds
            CloseHandle(pn_server_process_);
            pn_server_process_ = NULL;
        }
        
        pn_server_pid_ = 0;
        pn_server_running_ = false;
        send_json(client, "{\"success\":true}");
#else
        send_json(client, "{\"success\":false,\"message\":\"Not implemented on this platform\"}");
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
                // Process exited, reset state
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
    
    void handle_pn_connect(SOCKET client) {
        // Auto-connect to running PhoenixNest server
        if (pn_connected_) {
            send_json(client, "{\"success\":true,\"message\":\"Already connected\"}");
            return;
        }
        
        if (!pn_server_running_) {
            send_json(client, "{\"success\":false,\"message\":\"PhoenixNest server not running\"}");
            return;
        }
        
        if (pn_connect()) {
            std::ostringstream json;
            json << "{\"success\":true,\"message\":\"Connected to PhoenixNest\""
                 << ",\"ctrlPort\":" << pn_ctrl_port_
                 << ",\"dataPort\":" << pn_data_port_ << "}";
            send_json(client, json.str());
        } else {
            send_json(client, "{\"success\":false,\"message\":\"Failed to connect to PhoenixNest server\"}");
        }
    }
    
    void handle_pn_disconnect(SOCKET client) {
        pn_disconnect();
        send_json(client, "{\"success\":true,\"message\":\"Disconnected from PhoenixNest\"}");
    }
    
    // ============ MS-DMT INTEROP HANDLERS ============
    
    void handle_msdmt_connect(SOCKET client, const std::string& path) {
        // Parse parameters
        std::string host = "localhost";
        int ctrl_port = 4999;
        int data_port = 4998;
        
        size_t pos = path.find("host=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            host = url_decode(path.substr(pos + 5, end - pos - 5));
        }
        
        pos = path.find("ctrl=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            ctrl_port = std::stoi(path.substr(pos + 5, end - pos - 5));
        }
        
        pos = path.find("data=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            data_port = std::stoi(path.substr(pos + 5, end - pos - 5));
        }
        
        // Close existing connections
        if (msdmt_ctrl_sock_ != INVALID_SOCKET) {
            closesocket(msdmt_ctrl_sock_);
            msdmt_ctrl_sock_ = INVALID_SOCKET;
        }
        if (msdmt_data_sock_ != INVALID_SOCKET) {
            closesocket(msdmt_data_sock_);
            msdmt_data_sock_ = INVALID_SOCKET;
        }
        msdmt_connected_ = false;
        
        // Resolve host
        struct addrinfo hints{}, *result;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        if (getaddrinfo(host.c_str(), std::to_string(ctrl_port).c_str(), &hints, &result) != 0) {
            send_json(client, "{\"success\":false,\"message\":\"Cannot resolve host\"}");
            return;
        }
        
        // Connect to control port
        msdmt_ctrl_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (msdmt_ctrl_sock_ == INVALID_SOCKET) {
            freeaddrinfo(result);
            send_json(client, "{\"success\":false,\"message\":\"Socket creation failed\"}");
            return;
        }
        
        // Set timeout
        DWORD timeout = 5000;
        setsockopt(msdmt_ctrl_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(msdmt_ctrl_sock_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
        
        if (connect(msdmt_ctrl_sock_, result->ai_addr, (int)result->ai_addrlen) != 0) {
            closesocket(msdmt_ctrl_sock_);
            msdmt_ctrl_sock_ = INVALID_SOCKET;
            freeaddrinfo(result);
            send_json(client, "{\"success\":false,\"message\":\"Cannot connect to control port " + 
                      std::to_string(ctrl_port) + "\"}");
            return;
        }
        freeaddrinfo(result);
        
        // Connect to data port
        if (getaddrinfo(host.c_str(), std::to_string(data_port).c_str(), &hints, &result) != 0) {
            closesocket(msdmt_ctrl_sock_);
            msdmt_ctrl_sock_ = INVALID_SOCKET;
            send_json(client, "{\"success\":false,\"message\":\"Cannot resolve data port\"}");
            return;
        }
        
        msdmt_data_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (msdmt_data_sock_ == INVALID_SOCKET) {
            closesocket(msdmt_ctrl_sock_);
            msdmt_ctrl_sock_ = INVALID_SOCKET;
            freeaddrinfo(result);
            send_json(client, "{\"success\":false,\"message\":\"Data socket creation failed\"}");
            return;
        }
        
        setsockopt(msdmt_data_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(msdmt_data_sock_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
        
        if (connect(msdmt_data_sock_, result->ai_addr, (int)result->ai_addrlen) != 0) {
            closesocket(msdmt_ctrl_sock_);
            msdmt_ctrl_sock_ = INVALID_SOCKET;
            closesocket(msdmt_data_sock_);
            msdmt_data_sock_ = INVALID_SOCKET;
            freeaddrinfo(result);
            send_json(client, "{\"success\":false,\"message\":\"Cannot connect to data port " + 
                      std::to_string(data_port) + "\"}");
            return;
        }
        freeaddrinfo(result);
        
        // Wait for MODEM READY on control port
        char buf[1024];
        int n = recv(msdmt_ctrl_sock_, buf, sizeof(buf) - 1, 0);
        std::string ready_msg = "Connected";
        if (n > 0) {
            buf[n] = '\0';
            ready_msg = buf;
            // Trim whitespace
            while (!ready_msg.empty() && (ready_msg.back() == '\n' || ready_msg.back() == '\r')) {
                ready_msg.pop_back();
            }
        }
        
        msdmt_host_ = host;
        msdmt_ctrl_port_ = ctrl_port;
        msdmt_data_port_ = data_port;
        msdmt_connected_ = true;
        
        // Escape message for JSON
        std::string escaped;
        for (char c : ready_msg) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c >= 32) escaped += c;
        }
        
        send_json(client, "{\"success\":true,\"message\":\"" + escaped + "\"}");
    }
    
    void handle_msdmt_disconnect(SOCKET client) {
        if (msdmt_ctrl_sock_ != INVALID_SOCKET) {
            closesocket(msdmt_ctrl_sock_);
            msdmt_ctrl_sock_ = INVALID_SOCKET;
        }
        if (msdmt_data_sock_ != INVALID_SOCKET) {
            closesocket(msdmt_data_sock_);
            msdmt_data_sock_ = INVALID_SOCKET;
        }
        msdmt_connected_ = false;
        send_json(client, "{\"success\":true}");
    }
    
    bool msdmt_send_cmd(const std::string& cmd) {
        if (msdmt_ctrl_sock_ == INVALID_SOCKET) return false;
        std::string msg = cmd + "\n";
        std::cout << "[MSDMT] SEND: " << cmd << "\n";
        return send(msdmt_ctrl_sock_, msg.c_str(), (int)msg.size(), 0) > 0;
    }
    
    std::string msdmt_recv_ctrl(int timeout_ms = 5000) {
        if (msdmt_ctrl_sock_ == INVALID_SOCKET) return "";
        
        DWORD timeout = timeout_ms;
        setsockopt(msdmt_ctrl_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        
        char buf[4096];
        int n = recv(msdmt_ctrl_sock_, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            std::string result(buf);
            // Trim for display
            std::string display = result;
            while (!display.empty() && (display.back() == '\n' || display.back() == '\r')) display.pop_back();
            std::cout << "[MSDMT] RECV: " << display << "\n";
            return result;
        }
        std::cout << "[MSDMT] RECV: (timeout/error)\n";
        return "";
    }
    
    std::vector<uint8_t> msdmt_recv_data(int timeout_ms = 10000) {
        std::vector<uint8_t> data;
        if (msdmt_data_sock_ == INVALID_SOCKET) return data;
        
        DWORD timeout = timeout_ms;
        setsockopt(msdmt_data_sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        
        char buf[8192];
        while (true) {
            int n = recv(msdmt_data_sock_, buf, sizeof(buf), 0);
            if (n <= 0) break;
            data.insert(data.end(), buf, buf + n);
        }
        return data;
    }
    
    // PhoenixNest server connection functions
    bool pn_connect() {
        if (pn_connected_) return true;
        
        // Connect to control port
        pn_ctrl_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (pn_ctrl_sock_ == INVALID_SOCKET) return false;
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(pn_ctrl_port_);
        inet_pton(AF_INET, pn_host_.c_str(), &addr.sin_addr);
        
        if (connect(pn_ctrl_sock_, (sockaddr*)&addr, sizeof(addr)) != 0) {
            closesocket(pn_ctrl_sock_);
            pn_ctrl_sock_ = INVALID_SOCKET;
            return false;
        }
        
        // Wait for MODEM READY on control port (server sends this on connect)
        char buf[1024];
        int n = recv(pn_ctrl_sock_, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            closesocket(pn_ctrl_sock_);
            pn_ctrl_sock_ = INVALID_SOCKET;
            return false;
        }
        buf[n] = '\0';
        std::cout << "[PN] Control connected, received: " << buf << std::flush;
        
        // Connect to data port
        pn_data_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (pn_data_sock_ == INVALID_SOCKET) {
            closesocket(pn_ctrl_sock_);
            pn_ctrl_sock_ = INVALID_SOCKET;
            return false;
        }
        
        addr.sin_port = htons(pn_data_port_);
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
            // Trim for display
            std::string display = result;
            while (!display.empty() && (display.back() == '\n' || display.back() == '\r')) display.pop_back();
            std::cout << "[PN] RECV: " << display << "\n";
            return result;
        }
        std::cout << "[PN] RECV: (timeout/error)\n";
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
    
    void handle_msdmt_test1(SOCKET client, const std::string& path) {
        // Test 1: MS-DMT TX → PhoenixNest RX
        // This is a streaming SSE endpoint
        
        // Parse parameters
        std::string mode = "600S", message = "TEST", txdir = "./tx_pcm_out";
        
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
        
        pos = path.find("txdir=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            txdir = url_decode(path.substr(pos + 6, end - pos - 6));
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
        
        // Step 0: Set data rate
        send_sse("{\"step\":0,\"status\":\"running\",\"log\":\"Setting data rate: " + mode + "\",\"logType\":\"tx\"}");
        if (!msdmt_send_cmd("CMD:DATA RATE:" + mode)) {
            send_sse("{\"step\":0,\"status\":\"error\",\"result\":\"Failed to send data rate command\",\"success\":false}");
            return;
        }
        std::string resp = msdmt_recv_ctrl(2000);
        if (resp.find("OK:DATA RATE") == std::string::npos) {
            send_sse("{\"step\":0,\"status\":\"error\",\"result\":\"Data rate not acknowledged\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":0,\"status\":\"complete\",\"log\":\"Response: " + resp.substr(0, 50) + "\",\"logType\":\"rx\"}");
        
        // Step 1: Enable TX recording
        send_sse("{\"step\":1,\"status\":\"running\",\"log\":\"Enabling TX recording\",\"logType\":\"tx\"}");
        msdmt_send_cmd("CMD:RECORD TX:ON");
        resp = msdmt_recv_ctrl(2000);
        send_sse("{\"step\":1,\"status\":\"complete\"}");
        
        // Step 2: Send test message
        send_sse("{\"step\":2,\"status\":\"running\",\"log\":\"Sending message: " + message + "\",\"logType\":\"tx\"}");
        if (msdmt_data_sock_ != INVALID_SOCKET) {
            send(msdmt_data_sock_, message.c_str(), (int)message.size(), 0);
        }
        send_sse("{\"step\":2,\"status\":\"complete\"}");
        
        // Step 3: Trigger SENDBUFFER
        send_sse("{\"step\":3,\"status\":\"running\",\"log\":\"Triggering SENDBUFFER\",\"logType\":\"tx\"}");
        msdmt_send_cmd("CMD:SENDBUFFER");
        send_sse("{\"step\":3,\"status\":\"complete\"}");
        
        // Step 4: Wait for TX:IDLE
        send_sse("{\"step\":4,\"status\":\"running\",\"log\":\"Waiting for TX:IDLE...\",\"logType\":\"info\"}");
        bool tx_idle = false;
        for (int i = 0; i < 60; i++) {  // Up to 60 seconds
            resp = msdmt_recv_ctrl(1000);
            if (resp.find("STATUS:TX:IDLE") != std::string::npos) {
                tx_idle = true;
                break;
            }
            if (resp.find("STATUS:TX:TRANSMIT") != std::string::npos) {
                send_sse("{\"log\":\"TX in progress...\",\"logType\":\"info\"}");
            }
        }
        if (!tx_idle) {
            send_sse("{\"step\":4,\"status\":\"error\",\"result\":\"Timeout waiting for TX:IDLE\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":4,\"status\":\"complete\",\"log\":\"TX complete\",\"logType\":\"rx\"}");
        
        // Step 5: Find and load PCM file
        send_sse("{\"step\":5,\"status\":\"running\",\"log\":\"Looking for PCM in " + txdir + "\",\"logType\":\"info\"}");
        
        // Find most recent PCM file in tx_pcm_out
        std::string pcm_path;
        std::filesystem::file_time_type newest_time;
        
        try {
            for (const auto& entry : fs::directory_iterator(txdir)) {
                if (entry.path().extension() == ".pcm") {
                    auto ftime = fs::last_write_time(entry);
                    if (pcm_path.empty() || ftime > newest_time) {
                        pcm_path = entry.path().string();
                        newest_time = ftime;
                    }
                }
            }
        } catch (...) {}
        
        if (pcm_path.empty()) {
            send_sse("{\"step\":5,\"status\":\"error\",\"result\":\"No PCM file found in " + txdir + "\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":5,\"status\":\"complete\",\"log\":\"Found: " + pcm_path + "\",\"logType\":\"rx\"}");
        
        // Step 6: Connect to PhoenixNest server if needed
        send_sse("{\"step\":6,\"status\":\"running\",\"log\":\"Connecting to PhoenixNest server\",\"logType\":\"info\"}");
        
        if (!pn_connected_) {
            if (!pn_connect()) {
                send_sse("{\"step\":6,\"status\":\"error\",\"result\":\"Failed to connect to PhoenixNest server\",\"success\":false}");
                return;
            }
        }
        send_sse("{\"step\":6,\"status\":\"complete\",\"log\":\"Connected to PN on ports " + std::to_string(pn_ctrl_port_) + "/" + std::to_string(pn_data_port_) + "\",\"logType\":\"rx\"}");
        
        // Step 7: Inject PCM into PhoenixNest RX
        send_sse("{\"step\":7,\"status\":\"running\",\"log\":\"Injecting PCM into PhoenixNest RX\",\"logType\":\"tx\"}");
        
        fs::path abs_pcm = fs::absolute(pcm_path);
        pn_send_cmd("CMD:RXAUDIOINJECT:" + abs_pcm.string());
        resp = pn_recv_ctrl(2000);
        send_sse("{\"step\":7,\"status\":\"complete\",\"log\":\"Response: " + resp.substr(0, 60) + "\",\"logType\":\"rx\"}");
        
        // Step 8: Wait for DCD from PhoenixNest
        send_sse("{\"step\":8,\"status\":\"running\",\"log\":\"Waiting for DCD...\",\"logType\":\"info\"}");
        bool got_dcd = false;
        std::string detected_mode;
        for (int i = 0; i < 30; i++) {
            resp = pn_recv_ctrl(1000);
            if (resp.find("STATUS:RX:") != std::string::npos && resp.find("NO DCD") == std::string::npos) {
                got_dcd = true;
                // Extract mode from response
                size_t pos = resp.find("STATUS:RX:");
                if (pos != std::string::npos) {
                    detected_mode = resp.substr(pos + 10);
                    size_t end = detected_mode.find_first_of("\r\n");
                    if (end != std::string::npos) detected_mode = detected_mode.substr(0, end);
                }
                send_sse("{\"step\":8,\"status\":\"complete\",\"log\":\"DCD: " + detected_mode + "\",\"logType\":\"rx\"}");
                break;
            }
        }
        if (!got_dcd) {
            send_sse("{\"step\":8,\"status\":\"error\",\"result\":\"No DCD from PhoenixNest\",\"success\":false}");
            return;
        }
        
        // Step 9: Read decoded data from PhoenixNest
        send_sse("{\"step\":9,\"status\":\"running\",\"log\":\"Reading decoded data...\",\"logType\":\"info\"}");
        auto decoded = pn_recv_data(5000);
        std::string decoded_str(decoded.begin(), decoded.end());
        send_sse("{\"step\":9,\"status\":\"complete\",\"log\":\"Received " + std::to_string(decoded.size()) + " bytes\",\"logType\":\"rx\"}");
        
        // Step 10: Wait for NO DCD
        send_sse("{\"step\":10,\"status\":\"running\",\"log\":\"Waiting for end of signal...\",\"logType\":\"info\"}");
        for (int i = 0; i < 30; i++) {
            resp = pn_recv_ctrl(1000);
            if (resp.find("NO DCD") != std::string::npos) {
                break;
            }
        }
        send_sse("{\"step\":10,\"status\":\"complete\"}");
        
        // Step 11: Compare output
        send_sse("{\"step\":11,\"status\":\"running\",\"log\":\"Comparing output\",\"logType\":\"info\"}");
        
        bool match = (decoded_str.find(message) != std::string::npos);
        if (match) {
            send_sse("{\"step\":11,\"status\":\"complete\",\"result\":\"SUCCESS: Decoded '" + decoded_str.substr(0, 50) + "' matches!\",\"success\":true,\"decoded\":" + std::to_string(decoded.size()) + ",\"modeDetected\":\"" + detected_mode + "\"}");
        } else {
            send_sse("{\"step\":11,\"status\":\"error\",\"result\":\"MISMATCH: Expected '" + message + "', got '" + decoded_str.substr(0, 50) + "'\",\"success\":false,\"decoded\":" + std::to_string(decoded.size()) + "}");
        }
    }
    
    void handle_msdmt_test2(SOCKET client, const std::string& path) {
        // Test 2: PhoenixNest TX → MS-DMT RX
        
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
        
        // Step 0: Connect to PhoenixNest server if needed
        send_sse("{\"step\":0,\"status\":\"running\",\"log\":\"Connecting to PhoenixNest server\",\"logType\":\"info\"}");
        
        if (!pn_connected_) {
            if (!pn_connect()) {
                send_sse("{\"step\":0,\"status\":\"error\",\"result\":\"Failed to connect to PhoenixNest server\",\"success\":false}");
                return;
            }
        }
        send_sse("{\"step\":0,\"status\":\"complete\",\"log\":\"Connected to PN\",\"logType\":\"rx\"}");
        
        // Step 1: Set PhoenixNest data rate
        send_sse("{\"step\":1,\"status\":\"running\",\"log\":\"Setting data rate: " + mode + "\",\"logType\":\"tx\"}");
        pn_send_cmd("CMD:DATA RATE:" + mode);
        std::string resp = pn_recv_ctrl(2000);
        if (resp.find("OK:DATA RATE") == std::string::npos) {
            send_sse("{\"step\":1,\"status\":\"error\",\"result\":\"Data rate not acknowledged: " + resp + "\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":1,\"status\":\"complete\",\"log\":\"Response: " + resp.substr(0, 50) + "\",\"logType\":\"rx\"}");
        
        // Step 2: Enable TX recording
        send_sse("{\"step\":2,\"status\":\"running\",\"log\":\"Enabling TX recording\",\"logType\":\"tx\"}");
        pn_send_cmd("CMD:RECORD TX:ON");
        resp = pn_recv_ctrl(2000);
        send_sse("{\"step\":2,\"status\":\"complete\"}");
        
        // Step 3: Send test message to PhoenixNest
        send_sse("{\"step\":3,\"status\":\"running\",\"log\":\"Sending message: " + message + "\",\"logType\":\"tx\"}");
        if (pn_data_sock_ != INVALID_SOCKET) {
            send(pn_data_sock_, message.c_str(), (int)message.size(), 0);
        }
        send_sse("{\"step\":3,\"status\":\"complete\"}");
        
        // Step 4: Trigger SENDBUFFER
        send_sse("{\"step\":4,\"status\":\"running\",\"log\":\"Triggering SENDBUFFER\",\"logType\":\"tx\"}");
        pn_send_cmd("CMD:SENDBUFFER");
        send_sse("{\"step\":4,\"status\":\"complete\"}");
        
        // Step 5: Wait for TX:IDLE (server sends STATUS messages first, then OK:SENDBUFFER)
        send_sse("{\"step\":5,\"status\":\"running\",\"log\":\"Waiting for TX:IDLE...\",\"logType\":\"info\"}");
        bool tx_idle = false;
        for (int i = 0; i < 60; i++) {
            resp = pn_recv_ctrl(1000);
            if (resp.find("STATUS:TX:IDLE") != std::string::npos) {
                tx_idle = true;
                break;
            }
            if (resp.find("STATUS:TX:TRANSMIT") != std::string::npos) {
                send_sse("{\"log\":\"TX in progress...\",\"logType\":\"info\"}");
            }
        }
        if (!tx_idle) {
            send_sse("{\"step\":5,\"status\":\"error\",\"result\":\"Timeout waiting for TX:IDLE\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":5,\"status\":\"complete\",\"log\":\"TX complete\",\"logType\":\"rx\"}");
        
        // Step 6: Get SENDBUFFER response with PCM file path (comes after TX:IDLE)
        send_sse("{\"step\":6,\"status\":\"running\",\"log\":\"Getting TX PCM file path\",\"logType\":\"info\"}");
        
        std::string sendbuffer_resp = pn_recv_ctrl(2000);
        std::string pcm_path;
        size_t file_pos = sendbuffer_resp.find("FILE:");
        if (file_pos != std::string::npos) {
            pcm_path = sendbuffer_resp.substr(file_pos + 5);
            // Trim whitespace/newlines
            size_t end = pcm_path.find_first_of("\r\n");
            if (end != std::string::npos) pcm_path = pcm_path.substr(0, end);
        }
        
        if (pcm_path.empty()) {
            send_sse("{\"step\":6,\"status\":\"error\",\"result\":\"No TX PCM file path in SENDBUFFER response: " + sendbuffer_resp.substr(0, 60) + "\",\"success\":false}");
            return;
        }
        
        if (!fs::exists(pcm_path)) {
            send_sse("{\"step\":6,\"status\":\"error\",\"result\":\"TX PCM file not found: " + pcm_path + "\",\"success\":false}");
            return;
        }
        send_sse("{\"step\":6,\"status\":\"complete\",\"log\":\"PCM: " + pcm_path + "\",\"logType\":\"rx\"}");
        
        // Step 7: Inject PCM into MS-DMT RX
        send_sse("{\"step\":7,\"status\":\"running\",\"log\":\"Injecting PCM into MS-DMT RX\",\"logType\":\"tx\"}");
        
        fs::path abs_path = fs::absolute(pcm_path);
        msdmt_send_cmd("CMD:RXAUDIOINJECT:" + abs_path.string());
        resp = msdmt_recv_ctrl(2000);
        send_sse("{\"step\":7,\"status\":\"complete\",\"log\":\"Response: " + resp.substr(0, 60) + "\",\"logType\":\"rx\"}");
        
        // Step 8: Wait for STATUS:RX:<mode> or RXAUDIOINJECT:COMPLETE
        send_sse("{\"step\":8,\"status\":\"running\",\"log\":\"Waiting for DCD or completion...\",\"logType\":\"info\"}");
        bool got_dcd = false;
        bool inject_complete = false;
        std::string detected_mode;
        int octets_decoded = 0;
        
        for (int i = 0; i < 30; i++) {
            resp = msdmt_recv_ctrl(1000);
            
            // Check for RXAUDIOINJECT:COMPLETE (injection finished)
            if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                inject_complete = true;
                // Extract octets decoded
                size_t oct_pos = resp.find("octets decoded");
                if (oct_pos != std::string::npos) {
                    // Find the number before "octets decoded"
                    size_t comma_pos = resp.rfind(',', oct_pos);
                    if (comma_pos != std::string::npos) {
                        std::string oct_str = resp.substr(comma_pos + 1, oct_pos - comma_pos - 1);
                        // Trim whitespace
                        size_t start = oct_str.find_first_not_of(" ");
                        if (start != std::string::npos) {
                            oct_str = oct_str.substr(start);
                        }
                        try { octets_decoded = std::stoi(oct_str); } catch (...) {}
                    }
                }
                send_sse("{\"log\":\"Injection complete: " + std::to_string(octets_decoded) + " octets decoded\",\"logType\":\"info\"}");
                break;
            }
            
            // Check for DCD (carrier detected with mode)
            if (resp.find("STATUS:RX:") != std::string::npos && resp.find("NO DCD") == std::string::npos) {
                got_dcd = true;
                size_t pos = resp.find("STATUS:RX:");
                if (pos != std::string::npos) {
                    detected_mode = resp.substr(pos + 10);
                    size_t end = detected_mode.find_first_of("\r\n");
                    if (end != std::string::npos) detected_mode = detected_mode.substr(0, end);
                }
                send_sse("{\"log\":\"DCD: " + detected_mode + "\",\"logType\":\"rx\"}");
            }
        }
        
        // If we got DCD, wait for COMPLETE
        if (got_dcd && !inject_complete) {
            for (int i = 0; i < 30; i++) {
                resp = msdmt_recv_ctrl(1000);
                if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                    inject_complete = true;
                    size_t oct_pos = resp.find("octets decoded");
                    if (oct_pos != std::string::npos) {
                        size_t comma_pos = resp.rfind(',', oct_pos);
                        if (comma_pos != std::string::npos) {
                            std::string oct_str = resp.substr(comma_pos + 1, oct_pos - comma_pos - 1);
                            size_t start = oct_str.find_first_not_of(" ");
                            if (start != std::string::npos) oct_str = oct_str.substr(start);
                            try { octets_decoded = std::stoi(oct_str); } catch (...) {}
                        }
                    }
                    break;
                }
            }
        }
        
        if (!inject_complete) {
            send_sse("{\"step\":8,\"status\":\"error\",\"result\":\"RXAUDIOINJECT did not complete\",\"success\":false}");
            return;
        }
        
        if (octets_decoded == 0) {
            send_sse("{\"step\":8,\"status\":\"error\",\"result\":\"No data decoded (0 octets) - possible interop failure\",\"success\":false,\"modeDetected\":\"" + (got_dcd ? detected_mode : "NO DCD") + "\"}");
            return;
        }
        
        send_sse("{\"step\":8,\"status\":\"complete\",\"log\":\"Decoded " + std::to_string(octets_decoded) + " octets\",\"logType\":\"rx\"}");
        
        // Step 9: Read decoded data
        send_sse("{\"step\":9,\"status\":\"running\",\"log\":\"Reading decoded data...\",\"logType\":\"info\"}");
        auto decoded = msdmt_recv_data(5000);
        std::string decoded_str(decoded.begin(), decoded.end());
        send_sse("{\"step\":9,\"status\":\"complete\",\"log\":\"Received " + std::to_string(decoded.size()) + " bytes\",\"logType\":\"rx\"}");
        
        // Step 10: Wait for NO DCD
        send_sse("{\"step\":10,\"status\":\"running\",\"log\":\"Waiting for end of signal...\",\"logType\":\"info\"}");
        for (int i = 0; i < 30; i++) {
            resp = msdmt_recv_ctrl(1000);
            if (resp.find("NO DCD") != std::string::npos) {
                break;
            }
        }
        send_sse("{\"step\":10,\"status\":\"complete\"}");
        
        // Step 11: Compare output
        send_sse("{\"step\":11,\"status\":\"running\",\"log\":\"Comparing output\",\"logType\":\"info\"}");
        
        bool match = (decoded_str.find(message) != std::string::npos);
        if (match) {
            send_sse("{\"step\":11,\"status\":\"complete\",\"result\":\"SUCCESS: Decoded '" + decoded_str.substr(0, 50) + "' matches!\",\"success\":true,\"decoded\":" + std::to_string(decoded.size()) + ",\"modeDetected\":\"" + detected_mode + "\"}");
        } else {
            send_sse("{\"step\":11,\"status\":\"error\",\"result\":\"MISMATCH: Expected '" + message + 
                    "', got '" + decoded_str.substr(0, 50) + "'\",\"success\":false,\"decoded\":" + std::to_string(decoded.size()) + "}");
        }
    }
    
    void handle_msdmt_test1_quick(SOCKET client, const std::string& path) {
        // Quick version for matrix testing - MS-DMT TX → PhoenixNest RX
        std::string mode = "600S", message = "TEST", txdir = "./tx_pcm_out";
        
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
        
        pos = path.find("txdir=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            txdir = url_decode(path.substr(pos + 6, end - pos - 6));
        }
        
        if (!msdmt_connected_) {
            send_json(client, "{\"success\":false,\"error\":\"Not connected to MS-DMT\"}");
            return;
        }
        
        // Step 1: MS-DMT TX
        msdmt_send_cmd("CMD:DATA RATE:" + mode);
        std::string resp = msdmt_recv_ctrl(2000);
        if (resp.find("OK:DATA RATE") == std::string::npos) {
            send_json(client, "{\"success\":false,\"error\":\"Data rate not set\"}");
            return;
        }
        
        msdmt_send_cmd("CMD:RECORD TX:ON");
        msdmt_recv_ctrl(1000);
        
        if (msdmt_data_sock_ != INVALID_SOCKET) {
            send(msdmt_data_sock_, message.c_str(), (int)message.size(), 0);
        }
        
        msdmt_send_cmd("CMD:SENDBUFFER");
        
        // Wait for TX:IDLE
        bool tx_done = false;
        for (int i = 0; i < 60; i++) {
            resp = msdmt_recv_ctrl(1000);
            if (resp.find("STATUS:TX:IDLE") != std::string::npos) {
                tx_done = true;
                break;
            }
        }
        
        if (!tx_done) {
            send_json(client, "{\"success\":false,\"error\":\"TX timeout\"}");
            return;
        }
        
        // Step 2: Find PCM file
        std::string pcm_path;
        std::filesystem::file_time_type newest_time;
        try {
            for (const auto& entry : fs::directory_iterator(txdir)) {
                if (entry.path().extension() == ".pcm") {
                    auto ftime = fs::last_write_time(entry);
                    if (pcm_path.empty() || ftime > newest_time) {
                        pcm_path = entry.path().string();
                        newest_time = ftime;
                    }
                }
            }
        } catch (...) {}
        
        if (pcm_path.empty()) {
            send_json(client, "{\"success\":false,\"error\":\"No PCM file found\"}");
            return;
        }
        
        // Step 3: Connect to PhoenixNest if needed
        if (!pn_connected_) {
            if (!pn_connect()) {
                send_json(client, "{\"success\":false,\"error\":\"Cannot connect to PhoenixNest\"}");
                return;
            }
        }
        
        // Step 4: Inject PCM into PhoenixNest RX
        fs::path abs_pcm = fs::absolute(pcm_path);
        pn_send_cmd("CMD:RXAUDIOINJECT:" + abs_pcm.string());
        pn_recv_ctrl(2000);
        
        // Step 5: Wait for DCD or RXAUDIOINJECT:COMPLETE
        bool got_dcd = false;
        bool inject_complete = false;
        std::string detected_mode;
        for (int i = 0; i < 30; i++) {
            resp = pn_recv_ctrl(1000);
            
            // Check for injection complete (means we're done, success or not)
            if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                inject_complete = true;
                break;
            }
            
            // Check for DCD
            if (resp.find("STATUS:RX:") != std::string::npos && resp.find("NO DCD") == std::string::npos) {
                got_dcd = true;
                size_t mpos = resp.find("STATUS:RX:");
                if (mpos != std::string::npos) {
                    detected_mode = resp.substr(mpos + 10);
                    size_t end = detected_mode.find_first_of("\r\n");
                    if (end != std::string::npos) detected_mode = detected_mode.substr(0, end);
                }
            }
        }
        
        // If we got DCD, wait for COMPLETE
        if (got_dcd && !inject_complete) {
            for (int i = 0; i < 30; i++) {
                resp = pn_recv_ctrl(1000);
                if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                    inject_complete = true;
                    break;
                }
            }
        }
        
        if (!got_dcd) {
            send_json(client, "{\"success\":false,\"error\":\"No DCD from PhoenixNest\"}");
            return;
        }
        
        // Step 6: Read decoded data
        auto decoded = pn_recv_data(5000);
        std::string decoded_str(decoded.begin(), decoded.end());
        
        // Wait for NO DCD (skip if inject already completed)
        if (!inject_complete) {
            for (int i = 0; i < 30; i++) {
                resp = pn_recv_ctrl(1000);
                if (resp.find("NO DCD") != std::string::npos) break;
                if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) break;
            }
        }
        
        // Step 7: Compare
        bool match = (decoded_str.find(message) != std::string::npos);
        
        std::ostringstream json;
        json << "{\"success\":" << (match ? "true" : "false")
             << ",\"decoded\":" << decoded.size()
             << ",\"expected\":" << message.size()
             << ",\"modeDetected\":\"" << detected_mode << "\""
             << ",\"error\":\"" << (match ? "" : "Message mismatch") << "\"}";
        send_json(client, json.str());
    }
    
    void handle_msdmt_test2_quick(SOCKET client, const std::string& path) {
        // Quick version for matrix testing - PhoenixNest TX → MS-DMT RX
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
        
        if (!msdmt_connected_) {
            send_json(client, "{\"success\":false,\"error\":\"Not connected to MS-DMT\"}");
            return;
        }
        
        // Step 1: Connect to PhoenixNest if needed
        if (!pn_connected_) {
            if (!pn_connect()) {
                send_json(client, "{\"success\":false,\"error\":\"Cannot connect to PhoenixNest\"}");
                return;
            }
        }
        
        // Step 2: PhoenixNest TX
        pn_send_cmd("CMD:DATA RATE:" + mode);
        std::string resp = pn_recv_ctrl(2000);
        if (resp.find("OK:DATA RATE") == std::string::npos) {
            send_json(client, "{\"success\":false,\"error\":\"PN data rate not set: " + resp.substr(0, 30) + "\"}");
            return;
        }
        
        pn_send_cmd("CMD:RECORD TX:ON");
        pn_recv_ctrl(1000);
        
        if (pn_data_sock_ != INVALID_SOCKET) {
            send(pn_data_sock_, message.c_str(), (int)message.size(), 0);
        }
        
        pn_send_cmd("CMD:SENDBUFFER");
        
        // Wait for TX:IDLE first (server sends STATUS messages before OK:SENDBUFFER)
        bool tx_done = false;
        for (int i = 0; i < 60; i++) {
            resp = pn_recv_ctrl(1000);
            if (resp.find("STATUS:TX:IDLE") != std::string::npos) {
                tx_done = true;
                break;
            }
        }
        
        if (!tx_done) {
            send_json(client, "{\"success\":false,\"error\":\"PN TX timeout\"}");
            return;
        }
        
        // Now get SENDBUFFER response which includes the PCM file path (comes after TX:IDLE)
        std::string sendbuffer_resp = pn_recv_ctrl(2000);
        
        // Check for encode failure
        if (sendbuffer_resp.find("ERROR:SENDBUFFER") != std::string::npos || 
            sendbuffer_resp.find("ENCODE FAILED") != std::string::npos) {
            send_json(client, "{\"success\":false,\"error\":\"PhoenixNest encode failed\"}");
            return;
        }
        
        std::string pcm_path;
        size_t file_pos = sendbuffer_resp.find("FILE:");
        if (file_pos != std::string::npos) {
            pcm_path = sendbuffer_resp.substr(file_pos + 5);
            size_t end = pcm_path.find_first_of("\r\n");
            if (end != std::string::npos) pcm_path = pcm_path.substr(0, end);
        }
        
        // Step 3: Verify TX PCM file
        if (pcm_path.empty()) {
            send_json(client, "{\"success\":false,\"error\":\"No PCM path in SENDBUFFER response: " + sendbuffer_resp.substr(0, 40) + "\"}");
            return;
        }
        
        if (!fs::exists(pcm_path)) {
            send_json(client, "{\"success\":false,\"error\":\"PCM file not found: " + pcm_path + "\"}");
            return;
        }
        
        // Step 4: Inject PCM into MS-DMT RX
        fs::path abs_pcm = fs::absolute(pcm_path);
        msdmt_send_cmd("CMD:RXAUDIOINJECT:" + abs_pcm.string());
        msdmt_recv_ctrl(2000);
        
        // Step 5: Wait for DCD or RXAUDIOINJECT:COMPLETE
        bool got_dcd = false;
        bool inject_complete = false;
        std::string detected_mode;
        int octets_decoded = 0;
        
        for (int i = 0; i < 30; i++) {
            resp = msdmt_recv_ctrl(1000);
            
            // Check for injection complete
            if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                inject_complete = true;
                // Extract octets decoded
                size_t oct_pos = resp.find("octets decoded");
                if (oct_pos != std::string::npos) {
                    size_t comma_pos = resp.rfind(',', oct_pos);
                    if (comma_pos != std::string::npos) {
                        std::string oct_str = resp.substr(comma_pos + 1, oct_pos - comma_pos - 1);
                        size_t start = oct_str.find_first_not_of(" ");
                        if (start != std::string::npos) oct_str = oct_str.substr(start);
                        try { octets_decoded = std::stoi(oct_str); } catch (...) {}
                    }
                }
                break;
            }
            
            // Check for DCD
            if (resp.find("STATUS:RX:") != std::string::npos && resp.find("NO DCD") == std::string::npos) {
                got_dcd = true;
                size_t mpos = resp.find("STATUS:RX:");
                if (mpos != std::string::npos) {
                    detected_mode = resp.substr(mpos + 10);
                    size_t end = detected_mode.find_first_of("\r\n");
                    if (end != std::string::npos) detected_mode = detected_mode.substr(0, end);
                }
            }
        }
        
        // If we got DCD, wait for COMPLETE
        if (got_dcd && !inject_complete) {
            for (int i = 0; i < 30; i++) {
                resp = msdmt_recv_ctrl(1000);
                if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                    inject_complete = true;
                    size_t oct_pos = resp.find("octets decoded");
                    if (oct_pos != std::string::npos) {
                        size_t comma_pos = resp.rfind(',', oct_pos);
                        if (comma_pos != std::string::npos) {
                            std::string oct_str = resp.substr(comma_pos + 1, oct_pos - comma_pos - 1);
                            size_t start = oct_str.find_first_not_of(" ");
                            if (start != std::string::npos) oct_str = oct_str.substr(start);
                            try { octets_decoded = std::stoi(oct_str); } catch (...) {}
                        }
                    }
                    break;
                }
            }
        }
        
        if (!got_dcd || octets_decoded == 0) {
            send_json(client, "{\"success\":false,\"error\":\"No DCD from MS-DMT (0 octets decoded)\"}");
            return;
        }
        
        // Step 6: Read decoded data from MS-DMT
        auto decoded = msdmt_recv_data(5000);
        std::string decoded_str(decoded.begin(), decoded.end());
        
        // Wait for NO DCD
        for (int i = 0; i < 30; i++) {
            resp = msdmt_recv_ctrl(1000);
            if (resp.find("NO DCD") != std::string::npos) break;
        }
        
        // Step 7: Compare
        bool match = (decoded_str.find(message) != std::string::npos);
        
        std::ostringstream json;
        json << "{\"success\":" << (match ? "true" : "false")
             << ",\"decoded\":" << decoded.size()
             << ",\"expected\":" << message.size()
             << ",\"modeDetected\":\"" << detected_mode << "\""
             << ",\"error\":\"" << (match ? "" : "Message mismatch") << "\"}";
        send_json(client, json.str());
    }
    
    void handle_msdmt_ref_pcm(SOCKET client, const std::string& path) {
        // Extract mode parameter
        std::string mode = "600S";
        size_t pos = path.find("mode=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            mode = path.substr(pos + 5, end - pos - 5);
        }
        
        // Map mode to filename format (e.g., "600S" -> "tx_600S_...")
        std::map<std::string, std::string> modeToFile = {
            {"75S", "tx_75S_20251206_202410_888.pcm"},
            {"75L", "tx_75L_20251206_202421_539.pcm"},
            {"150S", "tx_150S_20251206_202440_580.pcm"},
            {"150L", "tx_150L_20251206_202446_986.pcm"},
            {"300S", "tx_300S_20251206_202501_840.pcm"},
            {"300L", "tx_300L_20251206_202506_058.pcm"},
            {"600S", "tx_600S_20251206_202518_709.pcm"},
            {"600L", "tx_600L_20251206_202521_953.pcm"},
            {"1200S", "tx_1200S_20251206_202533_636.pcm"},
            {"1200L", "tx_1200L_20251206_202536_295.pcm"},
            {"2400S", "tx_2400S_20251206_202547_345.pcm"},
            {"2400L", "tx_2400L_20251206_202549_783.pcm"}
        };
        
        // Expected message from metadata
        std::string expected_msg = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
        
        auto it = modeToFile.find(mode);
        if (it == modeToFile.end()) {
            send_json(client, "{\"success\":false,\"error\":\"Unknown mode: " + mode + "\"}");
            return;
        }
        
        // Build path to reference PCM
        fs::path ref_path = fs::current_path() / "refrence_pcm" / it->second;
        if (!fs::exists(ref_path)) {
            send_json(client, "{\"success\":false,\"error\":\"Reference PCM not found: " + ref_path.string() + "\"}");
            return;
        }
        
        fs::path abs_pcm = fs::absolute(ref_path);
        std::cout << "[MSDMT] Testing reference PCM: " << mode << " -> " << abs_pcm.string() << std::endl;
        
        // Convert mode format for MS-DMT (e.g., "600S" -> "600S", "600L" -> "600L")
        std::string msdmt_mode = mode;
        // Actually MS-DMT uses the same format, just add S/L
        
        // Set MS-DMT mode
        msdmt_send_cmd("CMD:DATA RATE:" + msdmt_mode);
        std::string resp = msdmt_recv_ctrl(2000);
        
        // Inject reference PCM into MS-DMT RX
        msdmt_send_cmd("CMD:RXAUDIOINJECT:" + abs_pcm.string());
        resp = msdmt_recv_ctrl(2000);
        
        // Wait for DCD or RXAUDIOINJECT:COMPLETE
        bool got_dcd = false;
        bool inject_complete = false;
        std::string detected_mode;
        int octets_decoded = 0;
        
        for (int i = 0; i < 45; i++) {  // Longer timeout for long interleave modes
            resp = msdmt_recv_ctrl(1000);
            
            // Check for injection complete
            if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                inject_complete = true;
                // Extract octets decoded
                size_t oct_pos = resp.find("octets decoded");
                if (oct_pos != std::string::npos) {
                    size_t comma_pos = resp.rfind(',', oct_pos);
                    if (comma_pos != std::string::npos) {
                        std::string oct_str = resp.substr(comma_pos + 1, oct_pos - comma_pos - 1);
                        size_t start = oct_str.find_first_not_of(" ");
                        if (start != std::string::npos) oct_str = oct_str.substr(start);
                        try { octets_decoded = std::stoi(oct_str); } catch (...) {}
                    }
                }
                break;
            }
            
            // Check for DCD
            if (resp.find("STATUS:RX:") != std::string::npos && resp.find("NO DCD") == std::string::npos) {
                got_dcd = true;
                size_t mpos = resp.find("STATUS:RX:");
                if (mpos != std::string::npos) {
                    detected_mode = resp.substr(mpos + 10);
                    size_t end = detected_mode.find_first_of("\r\n");
                    if (end != std::string::npos) detected_mode = detected_mode.substr(0, end);
                }
            }
        }
        
        // If we got DCD, wait for COMPLETE
        if (got_dcd && !inject_complete) {
            for (int i = 0; i < 30; i++) {
                resp = msdmt_recv_ctrl(1000);
                if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                    inject_complete = true;
                    size_t oct_pos = resp.find("octets decoded");
                    if (oct_pos != std::string::npos) {
                        size_t comma_pos = resp.rfind(',', oct_pos);
                        if (comma_pos != std::string::npos) {
                            std::string oct_str = resp.substr(comma_pos + 1, oct_pos - comma_pos - 1);
                            size_t start = oct_str.find_first_not_of(" ");
                            if (start != std::string::npos) oct_str = oct_str.substr(start);
                            try { octets_decoded = std::stoi(oct_str); } catch (...) {}
                        }
                    }
                    break;
                }
            }
        }
        
        // Read decoded data from MS-DMT data port
        std::string decoded_str;
        if (octets_decoded > 0) {
            auto decoded = msdmt_recv_data(5000);
            decoded_str = std::string(decoded.begin(), decoded.end());
        }
        
        // Wait for NO DCD to clean up
        for (int i = 0; i < 5; i++) {
            resp = msdmt_recv_ctrl(500);
            if (resp.find("NO DCD") != std::string::npos) break;
        }
        
        // Build response
        bool success = (octets_decoded > 0) && (decoded_str.find("QUICK BROWN FOX") != std::string::npos);
        
        std::ostringstream json;
        json << "{\"success\":" << (success ? "true" : "false")
             << ",\"decoded\":" << octets_decoded
             << ",\"expected\":" << expected_msg.length()
             << ",\"modeDetected\":\"" << detected_mode << "\""
             << ",\"gotDcd\":" << (got_dcd ? "true" : "false")
             << ",\"error\":\"" << (success ? "" : (octets_decoded == 0 ? "No DCD/decode" : "Message mismatch")) << "\"}";
        send_json(client, json.str());
    }
    
    void handle_pn_ref_pcm(SOCKET client, const std::string& path) {
        // Extract mode parameter
        std::string mode = "600S";
        size_t pos = path.find("mode=");
        if (pos != std::string::npos) {
            size_t end = path.find('&', pos);
            if (end == std::string::npos) end = path.size();
            mode = path.substr(pos + 5, end - pos - 5);
        }
        
        // Map mode to filename format (same reference PCMs as MS-DMT test)
        std::map<std::string, std::string> modeToFile = {
            {"75S", "tx_75S_20251206_202410_888.pcm"},
            {"75L", "tx_75L_20251206_202421_539.pcm"},
            {"150S", "tx_150S_20251206_202440_580.pcm"},
            {"150L", "tx_150L_20251206_202446_986.pcm"},
            {"300S", "tx_300S_20251206_202501_840.pcm"},
            {"300L", "tx_300L_20251206_202506_058.pcm"},
            {"600S", "tx_600S_20251206_202518_709.pcm"},
            {"600L", "tx_600L_20251206_202521_953.pcm"},
            {"1200S", "tx_1200S_20251206_202533_636.pcm"},
            {"1200L", "tx_1200L_20251206_202536_295.pcm"},
            {"2400S", "tx_2400S_20251206_202547_345.pcm"},
            {"2400L", "tx_2400L_20251206_202549_783.pcm"}
        };
        
        // Expected message from metadata
        std::string expected_msg = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890";
        
        auto it = modeToFile.find(mode);
        if (it == modeToFile.end()) {
            send_json(client, "{\"success\":false,\"error\":\"Unknown mode: " + mode + "\"}");
            return;
        }
        
        // Build path to reference PCM
        fs::path ref_path = fs::current_path() / "refrence_pcm" / it->second;
        if (!fs::exists(ref_path)) {
            send_json(client, "{\"success\":false,\"error\":\"Reference PCM not found: " + ref_path.string() + "\"}");
            return;
        }
        
        fs::path abs_pcm = fs::absolute(ref_path);
        std::cout << "[PN] Testing reference PCM: " << mode << " -> " << abs_pcm.string() << std::endl;
        
        // Connect to PhoenixNest server if not connected
        if (!pn_connected_) {
            if (!pn_connect()) {
                send_json(client, "{\"success\":false,\"error\":\"Cannot connect to PhoenixNest\"}");
                return;
            }
        }
        
        // Set PhoenixNest mode
        pn_send_cmd("CMD:DATA RATE:" + mode);
        std::string resp = pn_recv_ctrl(2000);
        
        // Inject reference PCM into PhoenixNest RX
        pn_send_cmd("CMD:RXAUDIOINJECT:" + abs_pcm.string());
        resp = pn_recv_ctrl(2000);
        
        // Wait for DCD or RXAUDIOINJECT:COMPLETE
        bool got_dcd = false;
        bool inject_complete = false;
        std::string detected_mode;
        int octets_decoded = 0;
        
        for (int i = 0; i < 45; i++) {
            resp = pn_recv_ctrl(1000);
            
            // Check for injection complete
            if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                inject_complete = true;
                // Extract octets decoded
                size_t oct_pos = resp.find("octets decoded");
                if (oct_pos != std::string::npos) {
                    size_t comma_pos = resp.rfind(',', oct_pos);
                    if (comma_pos != std::string::npos) {
                        std::string oct_str = resp.substr(comma_pos + 1, oct_pos - comma_pos - 1);
                        size_t start = oct_str.find_first_not_of(" ");
                        if (start != std::string::npos) oct_str = oct_str.substr(start);
                        try { octets_decoded = std::stoi(oct_str); } catch (...) {}
                    }
                }
                break;
            }
            
            // Check for DCD
            if (resp.find("STATUS:RX:") != std::string::npos && resp.find("NO DCD") == std::string::npos) {
                got_dcd = true;
                size_t mpos = resp.find("STATUS:RX:");
                if (mpos != std::string::npos) {
                    detected_mode = resp.substr(mpos + 10);
                    size_t end = detected_mode.find_first_of("\r\n");
                    if (end != std::string::npos) detected_mode = detected_mode.substr(0, end);
                }
            }
        }
        
        // If we got DCD, wait for COMPLETE
        if (got_dcd && !inject_complete) {
            for (int i = 0; i < 30; i++) {
                resp = pn_recv_ctrl(1000);
                if (resp.find("RXAUDIOINJECT:COMPLETE") != std::string::npos) {
                    inject_complete = true;
                    size_t oct_pos = resp.find("octets decoded");
                    if (oct_pos != std::string::npos) {
                        size_t comma_pos = resp.rfind(',', oct_pos);
                        if (comma_pos != std::string::npos) {
                            std::string oct_str = resp.substr(comma_pos + 1, oct_pos - comma_pos - 1);
                            size_t start = oct_str.find_first_not_of(" ");
                            if (start != std::string::npos) oct_str = oct_str.substr(start);
                            try { octets_decoded = std::stoi(oct_str); } catch (...) {}
                        }
                    }
                    break;
                }
            }
        }
        
        // Read decoded data from PhoenixNest data port
        std::string decoded_str;
        if (octets_decoded > 0) {
            auto decoded = pn_recv_data(5000);
            decoded_str = std::string(decoded.begin(), decoded.end());
        }
        
        // Wait for NO DCD to clean up
        for (int i = 0; i < 5; i++) {
            resp = pn_recv_ctrl(500);
            if (resp.find("NO DCD") != std::string::npos) break;
        }
        
        // Build response
        bool success = (octets_decoded > 0) && (decoded_str.find("QUICK BROWN FOX") != std::string::npos);
        
        std::ostringstream json;
        json << "{\"success\":" << (success ? "true" : "false")
             << ",\"decoded\":" << octets_decoded
             << ",\"expected\":" << expected_msg.length()
             << ",\"modeDetected\":\"" << detected_mode << "\""
             << ",\"gotDcd\":" << (got_dcd ? "true" : "false")
             << ",\"error\":\"" << (success ? "" : (octets_decoded == 0 ? "No DCD/decode" : "Message mismatch")) << "\"}";
        send_json(client, json.str());
    }
    
    void handle_save_interop_report(SOCKET client, const std::string& request) {
        // Find the body (after headers)
        size_t body_start = request.find("\r\n\r\n");
        if (body_start == std::string::npos) {
            send_json(client, "{\"success\":false,\"message\":\"Invalid request\"}");
            return;
        }
        
        std::string body = request.substr(body_start + 4);
        
        // Extract content from JSON
        std::string content;
        size_t content_pos = body.find("\"content\"");
        if (content_pos != std::string::npos) {
            size_t colon = body.find(':', content_pos);
            size_t quote1 = body.find('"', colon);
            size_t quote2 = std::string::npos;
            
            // Find closing quote, handling escaped quotes
            bool in_escape = false;
            for (size_t i = quote1 + 1; i < body.size(); i++) {
                if (in_escape) {
                    in_escape = false;
                    continue;
                }
                if (body[i] == '\\') {
                    in_escape = true;
                    continue;
                }
                if (body[i] == '"') {
                    quote2 = i;
                    break;
                }
            }
            
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                content = body.substr(quote1 + 1, quote2 - quote1 - 1);
                
                // Unescape newlines and special characters
                std::string unescaped;
                for (size_t i = 0; i < content.size(); i++) {
                    if (content[i] == '\\' && i + 1 < content.size()) {
                        char next = content[i + 1];
                        if (next == 'n') {
                            unescaped += '\n';
                            i++;
                        } else if (next == 'r') {
                            unescaped += '\r';
                            i++;
                        } else if (next == 't') {
                            unescaped += '\t';
                            i++;
                        } else if (next == '"') {
                            unescaped += '"';
                            i++;
                        } else if (next == '\\') {
                            unescaped += '\\';
                            i++;
                        } else {
                            unescaped += content[i];
                        }
                    } else {
                        unescaped += content[i];
                    }
                }
                content = unescaped;
            }
        }
        
        if (content.empty()) {
            send_json(client, "{\"success\":false,\"message\":\"No report content provided\"}");
            return;
        }
        
        // Generate filename with timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm_info = std::localtime(&time);
        char buf[64];
        std::strftime(buf, sizeof(buf), "interop_%Y%m%d_%H%M%S.md", tm_info);
        std::string filename = buf;
        
        // Ensure reports directory exists
        std::string reports_dir = exe_dir_ + "reports";
        if (!fs::exists(reports_dir)) {
            fs::create_directories(reports_dir);
        }
        
        std::string filepath = reports_dir + "/" + filename;
        
        std::ofstream file(filepath);
        if (!file.is_open()) {
            send_json(client, "{\"success\":false,\"message\":\"Failed to create report file\"}");
            return;
        }
        
        file << content;
        file.close();
        
        std::ostringstream json;
        json << "{\"success\":true,\"message\":\"Report saved\",\"filename\":\"" << filename << "\"}";
        send_json(client, json.str());
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
