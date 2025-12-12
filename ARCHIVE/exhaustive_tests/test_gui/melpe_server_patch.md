# MELPe Server Handlers Patch

## Instructions
Apply these sections to `server.h` in order:
1. Add helper functions (inside the TestGuiServer class, private section)
2. Add route handlers in handle_client() method
3. Add handler implementations (inside the TestGuiServer class, private section)

---

## 1. Helper Functions - Add in private section of TestGuiServer class

```cpp
    // ============ MELPE HELPER FUNCTIONS ============
    
    // Find melpe test audio directory - works in both dev and deployed scenarios
    std::string find_melpe_audio_dir() {
        std::vector<std::string> candidates = {
            exe_dir_ + "examples/melpe_test_audio/",
            exe_dir_ + "../examples/melpe_test_audio/",
            exe_dir_ + "../../src/melpe_core/test_audio/",
            exe_dir_ + "../src/melpe_core/test_audio/",
        };
        
        for (const auto& dir : candidates) {
            if (fs::exists(dir) && fs::is_directory(dir)) {
                try {
                    fs::path p = fs::canonical(dir);
                    std::string result = p.string();
                    if (!result.empty() && result.back() != '\\' && result.back() != '/') {
                        result += fs::path::preferred_separator;
                    }
                    return result;
                } catch (...) {
                    return dir;
                }
            }
        }
        return "";
    }
    
    // Find melpe_vocoder.exe - works in both dev and deployed scenarios
    std::string find_melpe_exe() {
        std::vector<std::string> candidates = {
            exe_dir_ + "melpe_vocoder.exe",
            exe_dir_ + "../bin/melpe_vocoder.exe",
            exe_dir_ + "../release/bin/melpe_vocoder.exe",
            exe_dir_ + "../../src/melpe_core/build/melpe_vocoder.exe",
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
    
    // Get recordings directory (creates if needed)
    std::string get_recordings_dir() {
        std::string audio_dir = find_melpe_audio_dir();
        if (audio_dir.empty()) {
            audio_dir = exe_dir_;
        }
        std::string rec_dir = audio_dir + "recordings" + std::string(1, fs::path::preferred_separator);
        
        if (!fs::exists(rec_dir)) {
            fs::create_directories(rec_dir);
        }
        return rec_dir;
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
```

---

## 2. Route Handlers - Add in handle_client() method

Find the routing section in `handle_client()` and add these routes:

```cpp
        // MELPe Vocoder routes
        else if (path.find("/melpe-audio?") == 0) {
            handle_melpe_audio(client, path);
        }
        else if (path.find("/melpe-run?") == 0) {
            handle_melpe_run(client, path);
        }
        else if (path.find("/melpe-output?") == 0) {
            handle_melpe_output(client, path);
        }
        else if (path == "/melpe-list-recordings") {
            handle_melpe_recordings(client);
        }
        else if (path == "/melpe-save-recording" && method == "POST") {
            handle_melpe_save_recording(client, request);
        }
```

---

## 3. Handler Implementations - Add in private section of TestGuiServer class

```cpp
    // ============ MELPE VOCODER HANDLERS ============
    
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
            filepath = audio_dir + "recordings" + std::string(1, fs::path::preferred_separator) + filename;
            file.open(filepath, std::ios::binary);
        }
        
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
            send_json(client, "{\"success\":false,\"message\":\"Invalid input file\"}");
            return;
        }
        
        if (rate != "600" && rate != "1200" && rate != "2400") {
            send_json(client, "{\"success\":false,\"message\":\"Invalid rate. Use 600, 1200, or 2400\"}");
            return;
        }
        
        std::string audio_dir = find_melpe_audio_dir();
        if (audio_dir.empty()) {
            send_json(client, "{\"success\":false,\"message\":\"MELPe test audio directory not found\"}");
            return;
        }
        
        std::string melpe_exe = find_melpe_exe();
        if (melpe_exe.empty()) {
            send_json(client, "{\"success\":false,\"message\":\"melpe_vocoder.exe not found\"}");
            return;
        }
        
        // Create output filename
        std::string output_file = "melpe_output_" + rate + "bps.raw";
        std::string input_path = audio_dir + input_file;
        
        // If not found in main dir, try recordings subdirectory
        if (!fs::exists(input_path)) {
            input_path = audio_dir + "recordings" + std::string(1, fs::path::preferred_separator) + input_file;
        }
        
        std::string output_path = exe_dir_ + output_file;
        
        if (!fs::exists(input_path)) {
            send_json(client, "{\"success\":false,\"message\":\"Input file not found: " + input_file + "\"}");
            return;
        }
        
        // Build command - run melpe_vocoder.exe in loopback mode
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
            _pclose(proc);
#else
            pclose(proc);
#endif
            
            if (fs::exists(output_path)) {
                auto input_size = fs::file_size(input_path);
                auto output_size = fs::file_size(output_path);
                std::ostringstream json;
                json << "{\"success\":true,\"message\":\"Processed " 
                     << std::fixed << std::setprecision(1) << (input_size / 2 / 8000.0) 
                     << "s of audio at " << rate << " bps\""
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
                send_json(client, "{\"success\":false,\"message\":\"Vocoder failed: " + escaped + "\"}");
            }
        } else {
            send_json(client, "{\"success\":false,\"message\":\"Could not start melpe_vocoder.exe\"}");
        }
    }
    
    void handle_melpe_output(SOCKET client, const std::string& path) {
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
    
    void handle_melpe_recordings(SOCKET client) {
        std::string rec_dir = get_recordings_dir();
        
        std::ostringstream json;
        json << "{\"recordings\":[";
        
        bool first = true;
        
        try {
            for (const auto& entry : fs::directory_iterator(rec_dir)) {
                if (entry.path().extension() == ".pcm" || entry.path().extension() == ".raw") {
                    if (!first) json << ",";
                    first = false;
                    
                    std::string fname = entry.path().filename().string();
                    auto filesize = fs::file_size(entry);
                    double duration = (filesize / 2) / 8000.0;  // 16-bit samples at 8kHz
                    
                    // Extract base name (remove timestamp and extension)
                    std::string name = entry.path().stem().string();
                    size_t underscore = name.rfind('_');
                    if (underscore != std::string::npos && underscore > 0) {
                        // Check if what follows looks like a timestamp
                        std::string suffix = name.substr(underscore + 1);
                        if (suffix.find("8k") != std::string::npos || suffix.length() > 10) {
                            name = name.substr(0, underscore);
                        }
                    }
                    
                    json << "{\"filename\":\"" << fname << "\","
                         << "\"name\":\"" << name << "\","
                         << "\"duration\":" << std::fixed << std::setprecision(1) << duration << "}";
                }
            }
        } catch (...) {}
        
        json << "]}";
        send_json(client, json.str());
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
        
        double duration = (pcm_data.size() / 2) / 8000.0;
        
        std::ostringstream json;
        json << "{\"success\":true,\"filename\":\"" << filename << "\",\"size\":" << pcm_data.size() 
             << ",\"duration\":" << std::fixed << std::setprecision(1) << duration << "}";
        send_json(client, json.str());
    }
```

---

## Required Includes

Make sure these includes are at the top of server.h:

```cpp
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace fs = std::filesystem;
```

---

## URL Decode Helper

If not already present, add this helper function:

```cpp
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
```
