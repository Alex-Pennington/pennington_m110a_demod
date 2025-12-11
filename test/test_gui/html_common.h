#pragma once
/**
 * @file html_common.h
 * @brief Shared CSS styles and JavaScript utilities for M110A Test GUI
 */

namespace test_gui {

// ============================================================
// COMMON CSS STYLES
// ============================================================
const char* HTML_CSS = R"CSS(
        * { box-sizing: border-box; }
        body { font-family: 'Consolas', 'Monaco', monospace; margin: 0; padding: 20px; background: #0a0a12; color: #c8d0e0; line-height: 1.4; }
        h1 { color: #00d4ff; margin-bottom: 20px; font-size: 24px; }
        h2 { color: #00d4ff; font-size: 14px; text-transform: uppercase; letter-spacing: 2px; margin: 0 0 12px 0; }
        h3 { color: #8892a8; font-size: 11px; text-transform: uppercase; letter-spacing: 1px; margin: 12px 0 8px 0; }
        .container { max-width: 1500px; margin: 0 auto; }
        
        /* Tabs */
        .tabs { display: flex; gap: 5px; margin-bottom: 0; flex-wrap: wrap; }
        .tab { padding: 12px 25px; background: #12121e; border: 1px solid #252538; border-bottom: none; border-radius: 8px 8px 0 0; color: #888; cursor: pointer; font-weight: bold; font-family: inherit; }
        .tab.active { color: #00d4ff; border-bottom: 2px solid #00d4ff; }
        .tab:hover { color: #00d4ff; }
        .tab-content { display: none; background: #12121e; border: 1px solid #252538; border-radius: 0 8px 8px 8px; padding: 20px; }
        .tab-content.active { display: block; }
        
        /* Layout */
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
        .output-line.warning { color: #ffaa00; }
        .output-line.error { color: #ff3a50; }
        
        /* Export bar */
        .export-bar { background: #0f0f1a; border: 1px solid #1e1e2e; border-radius: 4px; padding: 12px 16px; display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px; }
        .export-label { font-size: 11px; color: #6a7080; }
        .export-buttons { display: flex; gap: 8px; }
        
        /* Rating badge */
        .rating-badge { display: inline-block; padding: 4px 12px; border-radius: 4px; font-weight: bold; font-size: 14px; }
        .rating-excellent { background: #00ff88; color: #000; }
        .rating-good { background: #00d4ff; color: #000; }
        .rating-fair { background: #ffaa00; color: #000; }
        .rating-poor { background: #ff3a50; color: #fff; }
        
        /* Backend indicator */
        .backend-indicator { display: inline-block; padding: 2px 8px; border-radius: 3px; font-size: 10px; font-weight: bold; margin-left: 8px; }
        .backend-pn { background: #00d4ff; color: #000; }
        .backend-brain { background: #ff9f43; color: #000; }
)CSS";

// ============================================================
// COMMON JAVASCRIPT UTILITIES
// ============================================================
const char* HTML_JS_COMMON = R"JS(
        // Output log functions
        function addOutput(text, type = 'info') {
            const output = document.getElementById('output');
            const lines = text.split('\\n');
            for (const lineText of lines) {
                if (lineText.trim() === '') continue;
                const line = document.createElement('div');
                line.className = 'output-line ' + type;
                line.textContent = lineText;
                output.appendChild(line);
            }
            output.scrollTop = output.scrollHeight;
        }
        
        function clearOutput() { document.getElementById('output').innerHTML = ''; }
        function copyOutput() { navigator.clipboard.writeText(document.getElementById('output').innerText); }
        
        // Export functions
        function exportReport() { window.open('/export-report', '_blank'); }
        function exportCSV() { window.open('/export-csv', '_blank'); }
        function exportJSON() { window.open('/export-json', '_blank'); }
)JS";

} // namespace test_gui
