# M110A License Manager - API Integration Guide

This document describes the API endpoints available for integrating the **test_gui** application with the Phoenix Nest License Manager service.

**Base URL:** `https://www.organicengineer.com/software`

---

## Architecture Overview

The integration supports three main features:

1. **License Management** - Validate, request, and retrieve licenses programmatically
2. **Silent Report Upload** - One-click diagnostic report submission via API
3. **Embedded Web Interface** - Full issue tracker UI via WebView/browser panel

```
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚                        test_gui Application                      â”‚
â”œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¤
â”‚                                                                  â”‚
â”‚  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”    â”‚
â”‚  â”‚                    ON STARTUP                            â”‚    â”‚
â”‚  â”‚  1. POST /api/license/validate (check installed key)    â”‚    â”‚
â”‚  â”‚  2. If invalid â†’ POST /api/license/check (poll for new) â”‚    â”‚
â”‚  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜    â”‚
â”‚                                                                  â”‚
â”‚   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”         â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”     â”‚
â”‚   â”‚  Upload Report   â”‚         â”‚    Open Issue Tracker    â”‚     â”‚
â”‚   â”‚     Button       â”‚         â”‚        Button            â”‚     â”‚
â”‚   â””â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜         â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜     â”‚
â”‚            â”‚                                 â”‚                   â”‚
â”‚            â–¼                                 â–¼                   â”‚
â”‚   POST /api/report              Open WebView/Browser to         â”‚
â”‚   (silent, returns JSON)        /software/issues                â”‚
â”‚                                                                  â”‚
â”‚   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”                                          â”‚
â”‚   â”‚  Request License â”‚                                          â”‚
â”‚   â”‚     Button       â”‚                                          â”‚
â”‚   â””â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜                                          â”‚
â”‚            â”‚                                                     â”‚
â”‚            â–¼                                                     â”‚
â”‚   POST /api/license/request                                     â”‚
â”‚   (creates pending request)                                     â”‚
â”‚                                                                  â”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
```

---

## License Workflow

```
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”     â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”     â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”     â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚   test_gui   â”‚     â”‚  API Server  â”‚     â”‚    Admin     â”‚     â”‚   test_gui   â”‚
â”‚   startup    â”‚     â”‚              â”‚     â”‚   (manual)   â”‚     â”‚   startup    â”‚
â””â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”˜     â””â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”˜     â””â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”˜     â””â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”˜
       â”‚                    â”‚                    â”‚                    â”‚
       â”‚ POST /validate     â”‚                    â”‚                    â”‚
       â”‚ {key, hwid}        â”‚                    â”‚                    â”‚
       â”‚â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€>â”‚                    â”‚                    â”‚
       â”‚                    â”‚                    â”‚                    â”‚
       â”‚  {valid: false}    â”‚                    â”‚                    â”‚
       â”‚<â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”‚                    â”‚                    â”‚
       â”‚                    â”‚                    â”‚                    â”‚
       â”‚ POST /request      â”‚                    â”‚                    â”‚
       â”‚ {name, email, hwid}â”‚                    â”‚                    â”‚
       â”‚â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€>â”‚                    â”‚                    â”‚
       â”‚                    â”‚                    â”‚                    â”‚
       â”‚  {status: pending} â”‚                    â”‚                    â”‚
       â”‚<â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”‚                    â”‚                    â”‚
       â”‚                    â”‚                    â”‚                    â”‚
       â”‚                    â”‚   Review request   â”‚                    â”‚
       â”‚                    â”‚   Generate key     â”‚                    â”‚
       â”‚                    â”‚<â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”‚                    â”‚
       â”‚                    â”‚                    â”‚                    â”‚
       â”‚                    â”‚                    â”‚     (next startup) â”‚
       â”‚                    â”‚                    â”‚                    â”‚
       â”‚                    â”‚     POST /check    â”‚                    â”‚
       â”‚                    â”‚     {hwid, email}  â”‚                    â”‚
       â”‚                    â”‚<â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”‚
       â”‚                    â”‚                    â”‚                    â”‚
       â”‚                    â”‚  {found: true,     â”‚                    â”‚
       â”‚                    â”‚   license_key: X}  â”‚                    â”‚
       â”‚                    â”‚â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€>â”‚
       â”‚                    â”‚                    â”‚                    â”‚
       â”‚                    â”‚                    â”‚      Save & use    â”‚
       â”‚                    â”‚                    â”‚      license key   â”‚
```

---

## API Endpoints

### 1. Status Check

Check service availability and get endpoint URLs.

```
GET /software/api/status
```

**Response:**
```json
{
  "status": "ok",
  "service": "M110A License Manager",
  "version": "1.0.0",
  "endpoints": {
    "license_validate": "https://www.organicengineer.com/software/api/license/validate",
    "license_request": "https://www.organicengineer.com/software/api/license/request",
    "license_check": "https://www.organicengineer.com/software/api/license/check",
    "report_upload": "https://www.organicengineer.com/software/api/report",
    "issues": "https://www.organicengineer.com/software/issues",
    "docs": "https://www.organicengineer.com/software/docs/"
  }
}
```

---

## License API Endpoints

### 2. Validate License

Validate an installed license key against the hardware ID. **Call this on startup.**

```
POST /software/api/license/validate
Content-Type: application/json
```

**Request Body:**
```json
{
  "license_key": "CUST-1234567890ABCDEF-20251209-A1B2C3D4",
  "hardware_id": "1234567890ABCDEF"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `license_key` | string | **Yes** | The installed license key |
| `hardware_id` | string | **Yes** | The machine's hardware ID (8-16 hex chars) |

**Success Response:**
```json
{
  "valid": true,
  "message": "License is valid",
  "license_info": {
    "customer_name": "John Doe",
    "expiry_date": "2026-12-09",
    "is_perpetual": false
  }
}
```

**Failure Responses:**
```json
{
  "valid": false,
  "message": "License has expired"
}
```

```json
{
  "valid": false,
  "message": "Hardware ID mismatch"
}
```

```json
{
  "valid": false,
  "message": "License has been revoked"
}
```

```json
{
  "valid": false,
  "message": "License not found in database"
}
```

---

### 3. Request License

Submit a license request. Creates a pending request for manual approval.

```
POST /software/api/license/request
Content-Type: application/json
```

**Request Body:**
```json
{
  "customer_name": "John Doe",
  "email": "john@example.com",
  "hardware_id": "1234567890ABCDEF",
  "company": "Acme Corp",
  "use_case": "Amateur radio digital modes"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `customer_name` | string | **Yes** | Full name (min 2 chars) |
| `email` | string | **Yes** | Contact email address |
| `hardware_id` | string | **Yes** | Hardware ID (8-16 hex chars) |
| `company` | string | No | Company/organization name |
| `use_case` | string | No | Intended use description |

**Success Response - New Request (201):**
```json
{
  "success": true,
  "request_id": 42,
  "message": "License request submitted successfully. You will receive an email when approved.",
  "status": "pending"
}
```

**Success Response - Already Licensed:**
```json
{
  "success": true,
  "message": "A valid license already exists for this hardware ID",
  "status": "already_licensed",
  "license_key": "CUST-1234567890ABCDEF-20251209-A1B2C3D4"
}
```

**Success Response - Request Exists:**
```json
{
  "success": true,
  "request_id": 42,
  "message": "A license request for this hardware ID is already pending",
  "status": "already_exists"
}
```

---

### 4. Check for License

Check if a license has been issued for a hardware ID. **Use this to poll after requesting.**

```
POST /software/api/license/check
Content-Type: application/json
```

**Request Body:**
```json
{
  "hardware_id": "1234567890ABCDEF",
  "email": "john@example.com"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `hardware_id` | string | **Yes** | Hardware ID to check |
| `email` | string | No | Email for pending request lookup |

**Response - License Found:**
```json
{
  "found": true,
  "license_key": "CUST-1234567890ABCDEF-20251209-A1B2C3D4",
  "license_info": {
    "customer_name": "John Doe",
    "expiry_date": "2026-12-09",
    "is_perpetual": false,
    "created_at": "2025-12-09 10:30:00"
  },
  "pending_request": false,
  "message": "License found"
}
```

**Response - Request Pending:**
```json
{
  "found": false,
  "pending_request": true,
  "request_status": "pending",
  "message": "License request is pending approval"
}
```

**Response - Request Rejected:**
```json
{
  "found": false,
  "pending_request": false,
  "request_status": "rejected",
  "message": "License request was rejected"
}
```

**Response - Nothing Found:**
```json
{
  "found": false,
  "pending_request": false,
  "message": "No license or pending request found for this hardware ID"
}
```

---

## Report Upload API

### 5. Upload Diagnostic Report

Submit a diagnostic report programmatically. Creates an issue automatically.

```
POST /software/api/report
```

#### Option A: JSON Body

**Content-Type:** `application/json`

**Request Body:**
```json
{
  "report_content": "# M110A Diagnostic Report\n\n| Property | Value |\n|----------|-------|\n| Hardware ID | ABCD1234EFGH5678 |\n| Version | 1.2.0 |\n| Build | 111 |\n| Commit | a1b2c3d |\n\n## System Status\nDetails here...",
  "report_filename": "diagnostic_20251209_103045.md",
  "hwid": "ABCD1234EFGH5678",
  "version": "1.2.0"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `report_content` | string | **Yes** | The full diagnostic report content (markdown) |
| `report_filename` | string | No | Original filename (default: `diagnostic_report.md`) |
| `hwid` | string | No | Hardware ID (auto-extracted from report if not provided) |
| `version` | string | No | Software version (auto-extracted from report if not provided) |

#### Option B: Multipart Form Data

**Content-Type:** `multipart/form-data`

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `report` | file | **Yes** | The diagnostic report file (.md, .txt, .log, .json) |
| `hwid` | string | No | Hardware ID |
| `version` | string | No | Software version |

**Success Response (201 Created):**
```json
{
  "success": true,
  "issue_number": 3,
  "issue_url": "https://www.organicengineer.com/software/issues/3",
  "message": "Report uploaded successfully as issue #3"
}
```

**Error Responses:**

| Code | Response | Cause |
|------|----------|-------|
| 400 | `{"success": false, "message": "Missing report_content in JSON body"}` | No report content provided |
| 400 | `{"success": false, "message": "No file selected"}` | Empty file in multipart request |
| 400 | `{"success": false, "message": "Invalid file type..."}` | File extension not allowed |
| 400 | `{"success": false, "message": "File too large..."}` | File exceeds 5MB limit |
| 429 | Rate limit exceeded | More than 30 requests/hour |
| 500 | `{"success": false, "message": "Internal server error"}` | Server error |

---

## Auto-Extraction of Metadata

The API automatically parses the report content to extract:

- **Hardware ID** - From table rows or key-value patterns
- **Version** - Software version number
- **Build** - Build number
- **Commit** - Git commit hash

**Supported Patterns:**

```markdown
<!-- Table format (preferred) -->
| Hardware ID | ABCD1234EFGH5678 |
| Version | 1.2.0 |
| Build | 111 |
| Commit | a1b2c3d |

<!-- Key-value format -->
Hardware ID: ABCD1234EFGH5678
Version: 1.2.0
Build: 111
Commit: a1b2c3d

<!-- Alternative labels -->
HWID: ABCD1234EFGH5678
Software Version: v1.2.0
Build Number: 111
Git Commit: a1b2c3def
```

---

## Implementation Examples

### C++ License Manager Class

```cpp
#include <curl/curl.h>
#include <string>
#include <nlohmann/json.hpp>
#include <fstream>

class LicenseManager {
public:
    static const std::string BASE_URL;
    
    struct LicenseInfo {
        bool valid;
        std::string licenseKey;
        std::string customerName;
        std::string expiryDate;
        bool isPerpetual;
        std::string message;
    };
    
    struct RequestResult {
        bool success;
        std::string status;  // "pending", "already_licensed", "already_exists"
        std::string licenseKey;  // If already_licensed
        int requestId;
        std::string message;
    };
    
    // Validate installed license on startup
    static LicenseInfo validateLicense(const std::string& licenseKey, 
                                        const std::string& hardwareId) {
        LicenseInfo result = {false, "", "", "", false, ""};
        
        nlohmann::json payload;
        payload["license_key"] = licenseKey;
        payload["hardware_id"] = hardwareId;
        
        auto response = postJson("/api/license/validate", payload.dump());
        if (response.empty()) {
            result.message = "Network error";
            return result;
        }
        
        try {
            auto json = nlohmann::json::parse(response);
            result.valid = json["valid"].get<bool>();
            result.message = json["message"].get<std::string>();
            
            if (result.valid && json.contains("license_info")) {
                result.customerName = json["license_info"]["customer_name"];
                result.expiryDate = json["license_info"]["expiry_date"].is_null() 
                    ? "" : json["license_info"]["expiry_date"].get<std::string>();
                result.isPerpetual = json["license_info"]["is_perpetual"].get<bool>();
            }
        } catch (...) {
            result.message = "Failed to parse response";
        }
        
        return result;
    }
    
    // Request a new license
    static RequestResult requestLicense(const std::string& customerName,
                                         const std::string& email,
                                         const std::string& hardwareId,
                                         const std::string& company = "",
                                         const std::string& useCase = "") {
        RequestResult result = {false, "", "", 0, ""};
        
        nlohmann::json payload;
        payload["customer_name"] = customerName;
        payload["email"] = email;
        payload["hardware_id"] = hardwareId;
        if (!company.empty()) payload["company"] = company;
        if (!useCase.empty()) payload["use_case"] = useCase;
        
        auto response = postJson("/api/license/request", payload.dump());
        if (response.empty()) {
            result.message = "Network error";
            return result;
        }
        
        try {
            auto json = nlohmann::json::parse(response);
            result.success = json["success"].get<bool>();
            result.message = json["message"].get<std::string>();
            
            if (json.contains("status")) {
                result.status = json["status"].get<std::string>();
            }
            if (json.contains("license_key")) {
                result.licenseKey = json["license_key"].get<std::string>();
            }
            if (json.contains("request_id")) {
                result.requestId = json["request_id"].get<int>();
            }
        } catch (...) {
            result.message = "Failed to parse response";
        }
        
        return result;
    }
    
    // Check if license has been issued
    static LicenseInfo checkForLicense(const std::string& hardwareId,
                                        const std::string& email = "") {
        LicenseInfo result = {false, "", "", "", false, ""};
        
        nlohmann::json payload;
        payload["hardware_id"] = hardwareId;
        if (!email.empty()) payload["email"] = email;
        
        auto response = postJson("/api/license/check", payload.dump());
        if (response.empty()) {
            result.message = "Network error";
            return result;
        }
        
        try {
            auto json = nlohmann::json::parse(response);
            bool found = json["found"].get<bool>();
            result.message = json["message"].get<std::string>();
            
            if (found) {
                result.valid = true;
                result.licenseKey = json["license_key"].get<std::string>();
                if (json.contains("license_info")) {
                    result.customerName = json["license_info"]["customer_name"];
                    result.expiryDate = json["license_info"]["expiry_date"].is_null()
                        ? "" : json["license_info"]["expiry_date"].get<std::string>();
                    result.isPerpetual = json["license_info"]["is_perpetual"].get<bool>();
                }
            }
        } catch (...) {
            result.message = "Failed to parse response";
        }
        
        return result;
    }
    
private:
    static std::string postJson(const std::string& endpoint, const std::string& body) {
        CURL* curl = curl_easy_init();
        std::string response;
        
        if (!curl) return response;
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        std::string url = BASE_URL + endpoint;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
            +[](char* ptr, size_t size, size_t nmemb, std::string* data) -> size_t {
                data->append(ptr, size * nmemb);
                return size * nmemb;
            });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        return response;
    }
};

const std::string LicenseManager::BASE_URL = "https://www.organicengineer.com/software";

// ============ USAGE IN test_gui ============

// On application startup
void checkLicenseOnStartup() {
    std::string storedKey = loadLicenseKeyFromFile();  // Your function
    std::string hwid = getHardwareId();                 // Your function
    
    if (!storedKey.empty()) {
        // Validate existing license
        auto result = LicenseManager::validateLicense(storedKey, hwid);
        
        if (result.valid) {
            std::cout << "License valid for: " << result.customerName << std::endl;
            if (!result.isPerpetual) {
                std::cout << "Expires: " << result.expiryDate << std::endl;
            }
            return;  // All good
        } else {
            std::cout << "License invalid: " << result.message << std::endl;
        }
    }
    
    // No valid license - check if one has been issued
    auto checkResult = LicenseManager::checkForLicense(hwid, getUserEmail());
    
    if (checkResult.valid) {
        // License found! Save it
        saveLicenseKeyToFile(checkResult.licenseKey);
        std::cout << "License retrieved and installed!" << std::endl;
    } else {
        // No license yet - prompt user to request one
        showLicenseRequestDialog();
    }
}

// When user clicks "Request License"
void onRequestLicenseClicked(const std::string& name, 
                              const std::string& email,
                              const std::string& company) {
    std::string hwid = getHardwareId();
    
    auto result = LicenseManager::requestLicense(name, email, hwid, company);
    
    if (result.success) {
        if (result.status == "already_licensed") {
            // They already have a license!
            saveLicenseKeyToFile(result.licenseKey);
            showMessage("License found and installed!");
        } else if (result.status == "pending" || result.status == "already_exists") {
            showMessage("License request submitted. You'll be notified when approved.");
        }
    } else {
        showError("Request failed: " + result.message);
    }
}
```

### C++ Report Upload (libcurl)

```cpp
struct UploadResult {
    bool success;
    int issue_number;
    std::string issue_url;
    std::string message;
};

UploadResult uploadDiagnosticReport(const std::string& reportContent, 
                                     const std::string& filename,
                                     const std::string& hwid,
                                     const std::string& version) {
    CURL* curl = curl_easy_init();
    UploadResult result = {false, 0, "", ""};
    
    if (!curl) return result;
    
    // Build JSON payload
    nlohmann::json payload;
    payload["report_content"] = reportContent;
    payload["report_filename"] = filename;
    if (!hwid.empty()) payload["hwid"] = hwid;
    if (!version.empty()) payload["version"] = version;
    
    std::string jsonStr = payload.dump();
    std::string response;
    
    // Setup request
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, 
        "https://www.organicengineer.com/software/api/report");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, 
        +[](char* ptr, size_t size, size_t nmemb, std::string* data) -> size_t {
            data->append(ptr, size * nmemb);
            return size * nmemb;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        try {
            auto json = nlohmann::json::parse(response);
            result.success = json["success"].get<bool>();
            if (result.success) {
                result.issue_number = json["issue_number"].get<int>();
                result.issue_url = json["issue_url"].get<std::string>();
            }
            result.message = json["message"].get<std::string>();
        } catch (...) {
            result.message = "Failed to parse response";
        }
    } else {
        result.message = curl_easy_strerror(res);
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

// Usage in test_gui
void onUploadReportClicked() {
    std::string report = generateDiagnosticReport();  // Your existing function
    std::string filename = "diagnostic_" + getCurrentTimestamp() + ".md";
    
    auto result = uploadDiagnosticReport(
        report, 
        filename,
        getHardwareId(),    // Your existing function
        getVersionString()  // Your existing function
    );
    
    if (result.success) {
        showNotification("Report uploaded as #" + std::to_string(result.issue_number));
    } else {
        showError("Upload failed: " + result.message);
    }
}
```

### C++ with Qt (QNetworkAccessManager)

```cpp
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

class ReportUploader : public QObject {
    Q_OBJECT
public:
    void uploadReport(const QString& content, const QString& filename) {
        QNetworkAccessManager* manager = new QNetworkAccessManager(this);
        
        QJsonObject json;
        json["report_content"] = content;
        json["report_filename"] = filename;
        json["hwid"] = getHardwareId();
        json["version"] = getVersionString();
        
        QNetworkRequest request(QUrl("https://www.organicengineer.com/software/api/report"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        
        QNetworkReply* reply = manager->post(request, QJsonDocument(json).toJson());
        
        connect(reply, &QNetworkReply::finished, [this, reply]() {
            if (reply->error() == QNetworkReply::NoError) {
                QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
                if (response["success"].toBool()) {
                    emit uploadSuccess(
                        response["issue_number"].toInt(),
                        response["issue_url"].toString()
                    );
                } else {
                    emit uploadFailed(response["message"].toString());
                }
            } else {
                emit uploadFailed(reply->errorString());
            }
            reply->deleteLater();
        });
    }
    
signals:
    void uploadSuccess(int issueNumber, const QString& issueUrl);
    void uploadFailed(const QString& error);
};
```

### Python (for testing/scripts)

```python
import requests

def upload_report(report_content: str, filename: str = None, 
                  hwid: str = None, version: str = None) -> dict:
    """Upload a diagnostic report to the license manager."""
    
    url = "https://www.organicengineer.com/software/api/report"
    
    payload = {
        "report_content": report_content,
        "report_filename": filename or "diagnostic_report.md"
    }
    if hwid:
        payload["hwid"] = hwid
    if version:
        payload["version"] = version
    
    response = requests.post(url, json=payload)
    return response.json()

# Usage
result = upload_report(
    report_content=open("diagnostic.md").read(),
    filename="diagnostic.md",
    hwid="ABCD1234EFGH5678",
    version="1.2.0"
)

if result["success"]:
    print(f"Uploaded as issue #{result['issue_number']}")
    print(f"View at: {result['issue_url']}")
else:
    print(f"Failed: {result['message']}")
```

---

## Embedded Web Interface

For full issue tracker functionality (browsing, searching, commenting, submitting detailed bug reports), embed the web interface in a WebView or launch the user's browser.

### URLs for Embedding

| Feature | URL |
|---------|-----|
| Issue List | `https://www.organicengineer.com/software/issues` |
| New Issue (type chooser) | `https://www.organicengineer.com/software/issues/new` |
| Bug Report Form | `https://www.organicengineer.com/software/issues/new/bug` |
| Feature Request Form | `https://www.organicengineer.com/software/issues/new/feature` |
| Submit Diagnostic Report | `https://www.organicengineer.com/software/issues/new/report` |
| Ask a Question | `https://www.organicengineer.com/software/issues/new/question` |
| Documentation | `https://www.organicengineer.com/software/docs/` |
| Downloads | `https://www.organicengineer.com/software/download` |

### Qt WebView Example

```cpp
#include <QWebEngineView>

void openIssueTracker() {
    QWebEngineView* view = new QWebEngineView();
    view->setUrl(QUrl("https://www.organicengineer.com/software/issues"));
    view->resize(1024, 768);
    view->setWindowTitle("Phoenix Nest Issue Tracker");
    view->show();
}

// Or just open in system browser
void openInBrowser() {
    QDesktopServices::openUrl(
        QUrl("https://www.organicengineer.com/software/issues")
    );
}
```

---

## Rate Limits

| Endpoint | Limit |
|----------|-------|
| `POST /api/license/validate` | 60 requests per hour |
| `POST /api/license/request` | 5 requests per hour |
| `POST /api/license/check` | 30 requests per hour |
| `POST /api/report` | 30 requests per hour |
| `GET /api/status` | Unlimited |
| Web forms (bug, feature, etc.) | 10 submissions per hour |

---

## Issue Types

Reports uploaded via API are automatically tagged as `report` type for future automated analysis.

| Type | Description | How Created |
|------|-------------|-------------|
| `report` | Diagnostic reports from modem | API upload or web form |
| `bug` | Bug reports | Web form only |
| `feature` | Feature requests | Web form only |
| `question` | Support questions | Web form only |

---

## Recommended UI Flow for test_gui

```
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚                         Help Menu                                â”‚
â”œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¤
â”‚                                                                  â”‚
â”‚   ðŸ“Š Upload Diagnostic Report    â†’ POST /api/report (silent)    â”‚
â”‚      "Uploads current system state for analysis"                â”‚
â”‚                                                                  â”‚
â”‚   â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€                 â”‚
â”‚                                                                  â”‚
â”‚   ðŸ› Report a Bug               â†’ Open /issues/new/bug          â”‚
â”‚   ðŸ’¡ Request a Feature          â†’ Open /issues/new/feature      â”‚
â”‚   â“ Get Help                   â†’ Open /issues/new/question     â”‚
â”‚                                                                  â”‚
â”‚   â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€                 â”‚
â”‚                                                                  â”‚
â”‚   ðŸ“‹ View All Issues            â†’ Open /issues                  â”‚
â”‚   ðŸ“š Documentation              â†’ Open /docs/                   â”‚
â”‚                                                                  â”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
```

---

## Contact

For API issues or integration questions, submit an issue at:
https://www.organicengineer.com/software/issues/new/question
