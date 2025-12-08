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
        .controls { background: #16213e; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
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
    </style>
</head>
<body>
    <div class="container">
        <h1>M110A Modem Test Suite</h1>
        
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
    </div>
    
    <script>
        let eventSource = null;
        const ALL_MODES = ['75S','75L','150S','150L','300S','300L','600S','600L','1200S','1200L','2400S','2400L'];
        const ALL_EQS = ['DFE','NONE','DFE_RLS','MLSE_L2','MLSE_L3','MLSE_ADAPTIVE','TURBO'];
        
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
        } else {
            send_404(client);
        }
        
        closesocket(client);
    }
    
    void send_html(SOCKET client, const char* html) {
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: text/html\r\n"
                 << "Content-Length: " << strlen(html) << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << html;
        std::string resp = response.str();
        send(client, resp.c_str(), (int)resp.size(), 0);
    }
    
    void send_404(SOCKET client) {
        const char* html = "<html><body><h1>404 Not Found</h1></body></html>";
        std::ostringstream response;
        response << "HTTP/1.1 404 Not Found\r\n"
                 << "Content-Type: text/html\r\n"
                 << "Content-Length: " << strlen(html) << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << html;
        std::string resp = response.str();
        send(client, resp.c_str(), (int)resp.size(), 0);
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
        
        // Build command line - use full path to exhaustive_test.exe
        std::string cmd = "\"" + exe_dir_ + "exhaustive_test.exe\"";
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
