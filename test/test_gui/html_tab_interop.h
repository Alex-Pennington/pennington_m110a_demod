#pragma once
/**
 * @file html_tab_interop.h
 * @brief Cross-Modem Interop tab HTML/JS for M110A Test GUI
 */

namespace test_gui {

// ============================================================
// INTEROP TAB CSS
// ============================================================
const char* HTML_CSS_INTEROP = R"CSS(
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
        .log-info { color: #888; }
)CSS";

// ============================================================
// INTEROP TAB HTML
// ============================================================
const char* HTML_TAB_INTEROP = R"HTML(
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
                    <h3>🧠 G4GUO Modem</h3>
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
)HTML";

// ============================================================
// INTEROP TAB JAVASCRIPT
// ============================================================
const char* HTML_JS_INTEROP = R"JS(
        let pnServerRunning = false;
        let brainConnected = false;
        let interopRunning = false;
        
        function showSubTab(name) {
            document.querySelectorAll('.sub-tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.sub-tab-content').forEach(t => t.classList.remove('active'));
            document.querySelector('.sub-tab[onclick*="' + name + '"]').classList.add('active');
            document.getElementById('subtab-' + name).classList.add('active');
        }
        
        function interopLog(msg, type = 'info') {
            const log = document.getElementById('interop-log');
            const time = new Date().toLocaleTimeString();
            log.innerHTML += '<div class="log-' + type + '">[' + time + '] ' + msg + '</div>';
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
                const r = await fetch('/pn-server-start?ctrl=' + ctrl + '&data=' + data);
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
                const r = await fetch('/brain-connect?host=' + encodeURIComponent(host) + '&ctrl=' + ctrl + '&data=' + data);
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
        
        async function runMatrix() {
            if (interopRunning) return;
            interopRunning = true;
            
            const modes = ['75S','75L','150S','150L','300S','300L','600S','600L','1200S','1200L','2400S','2400L'];
            modes.forEach(m => {
                const cell1 = document.getElementById('cm-' + m + '-1');
                const cell2 = document.getElementById('cm-' + m + '-2');
                if (cell1) { cell1.className = 'matrix-cell matrix-pending'; cell1.textContent = '○'; }
                if (cell2) { cell2.className = 'matrix-cell matrix-pending'; cell2.textContent = '○'; }
            });
            
            document.getElementById('matrix-progress').textContent = '0/36';
            interopLog('Starting local interop test (brain_core embedded)...', 'info');
            
            try {
                const response = await fetch('/run-interop');
                const reader = response.body.getReader();
                const decoder = new TextDecoder();
                
                while (interopRunning) {
                    const { value, done } = await reader.read();
                    if (done) break;
                    
                    const text = decoder.decode(value);
                    for (const line of text.split('\n')) {
                        if (line.startsWith('data: ')) {
                            try {
                                const data = JSON.parse(line.substring(6));
                                handleInteropEvent(data);
                                if (data.done) interopRunning = false;
                            } catch (e) {}
                        }
                    }
                }
            } catch (err) {
                interopLog('Error: ' + err.message, 'error');
            }
            
            interopRunning = false;
            interopLog('Interop test complete', 'info');
        }
        
        function handleInteropEvent(data) {
            if (data.output) {
                let type = 'info';
                if (data.output.includes('PASS')) type = 'rx';
                else if (data.output.includes('FAIL')) type = 'error';
                interopLog(data.output, type);
            }
            
            if (data.type === 'interop_result') {
                const mode = data.mode;
                const cell1 = document.getElementById('cm-' + mode + '-1');
                const cell2 = document.getElementById('cm-' + mode + '-2');
                
                if (cell1) {
                    cell1.className = data.brain_pn ? 'matrix-cell matrix-pass' : 'matrix-cell matrix-fail';
                    cell1.textContent = data.brain_pn ? '✓' : '✗';
                }
                if (cell2) {
                    cell2.className = data.pn_brain ? 'matrix-cell matrix-pass' : 'matrix-cell matrix-fail';
                    cell2.textContent = data.pn_brain ? '✓' : '✗';
                }
            }
            
            if (data.progress !== undefined) {
                const total = 36;
                const done = Math.round(data.progress * total / 100);
                document.getElementById('matrix-progress').textContent = done + '/' + total;
            }
            
            if (data.passed !== undefined && data.total !== undefined) {
                document.getElementById('matrix-progress').textContent = data.passed + '/' + data.total + ' passed';
            }
        }
        
        function initInteropMatrix() {
            const modes = ['75S','75L','150S','150L','300S','300L','600S','600L','1200S','1200L','2400S','2400L'];
            const tbody = document.getElementById('matrix-body');
            if (tbody) {
                tbody.innerHTML = modes.map(m => '<tr><td>' + m + '</td><td class="matrix-cell matrix-pending" id="cm-' + m + '-1">○</td><td class="matrix-cell matrix-pending" id="cm-' + m + '-2">○</td></tr>').join('');
            }
            
            ['brain-pn-mode', 'pn-brain-mode'].forEach(id => {
                const sel = document.getElementById(id);
                if (sel) {
                    sel.innerHTML = modes.map(m => '<option value="' + m + '">' + m + '</option>').join('');
                    sel.value = '600S';
                }
            });
        }
)JS";

} // namespace test_gui
