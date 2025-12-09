/**
 * @file license.h
 * @brief Software licensing and activation system
 * 
 * Implements hardware-locked licensing with validation
 * Uses hardware fingerprint + customer ID + expiration date
 */

#ifndef M110A_LICENSE_H
#define M110A_LICENSE_H

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <ctime>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#else
#include <cpuid.h>
#include <unistd.h>
#include <sys/types.h>
#endif

namespace m110a {

/**
 * License validation result
 */
enum class LicenseStatus {
    VALID,              // License is valid and active
    INVALID_KEY,        // License key format invalid
    HARDWARE_MISMATCH,  // Hardware doesn't match license
    EXPIRED,            // License has expired
    NOT_FOUND,          // No license file found
    TAMPERED,           // License file has been modified
    TRIAL_EXPIRED       // Trial period expired
};

/**
 * License information
 */
struct LicenseInfo {
    std::string customer_id;
    std::string hardware_id;
    std::time_t expiration_date;
    bool is_trial;
    int max_channels;  // 0 = unlimited
    
    LicenseInfo() 
        : expiration_date(0)
        , is_trial(true)
        , max_channels(1)
    {}
};

/**
 * Hardware fingerprinting and license validation
 */
class LicenseManager {
public:
    /**
     * Get hardware fingerprint (CPU ID + MAC address hash)
     */
    static std::string get_hardware_id() {
        uint32_t cpu_info[4] = {0};
        
#ifdef _WIN32
        __cpuid((int*)cpu_info, 1);
#else
        __get_cpuid(1, &cpu_info[0], &cpu_info[1], &cpu_info[2], &cpu_info[3]);
#endif
        
        // Combine CPU features into fingerprint
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        ss << std::setw(8) << cpu_info[0];
        ss << std::setw(8) << cpu_info[3];
        
        return ss.str();
    }
    
    /**
     * Validate license key format and content
     * 
     * License key format: CUSTOMER-HWID-EXPIRY-CHECKSUM
     * Example: ACME01-A3B4C5D6-20261231-9F8E7D6C
     */
    static LicenseStatus validate_license(const std::string& license_key, LicenseInfo& info) {
        // Parse license key
        if (license_key.empty()) {
            return LicenseStatus::NOT_FOUND;
        }
        
        // Split key into components
        std::vector<std::string> parts = split_string(license_key, '-');
        if (parts.size() != 4) {
            return LicenseStatus::INVALID_KEY;
        }
        
        std::string customer = parts[0];
        std::string hw_id = parts[1];
        std::string expiry_str = parts[2];
        std::string checksum = parts[3];
        
        // Validate checksum
        std::string expected_checksum = compute_checksum(customer + hw_id + expiry_str);
        if (checksum != expected_checksum) {
            return LicenseStatus::TAMPERED;
        }
        
        // Check hardware match
        std::string current_hw = get_hardware_id();
        if (hw_id != current_hw) {
            return LicenseStatus::HARDWARE_MISMATCH;
        }
        
        // Check expiration
        std::time_t expiry = parse_date(expiry_str);
        std::time_t now = std::time(nullptr);
        if (expiry < now) {
            return LicenseStatus::EXPIRED;
        }
        
        // Populate license info
        info.customer_id = customer;
        info.hardware_id = hw_id;
        info.expiration_date = expiry;
        info.is_trial = false;
        info.max_channels = 0;  // Unlimited
        
        return LicenseStatus::VALID;
    }
    
    /**
     * Generate license key (for admin tool)
     * 
     * @param customer_id Customer identifier (e.g., "ACME01")
     * @param hardware_id Hardware fingerprint
     * @param days_valid Days until expiration (0 = 1 year)
     */
    static std::string generate_license_key(
        const std::string& customer_id,
        const std::string& hardware_id,
        int days_valid = 365)
    {
        // Calculate expiration date
        std::time_t now = std::time(nullptr);
        std::time_t expiry = now + (days_valid * 24 * 60 * 60);
        
        std::tm* expiry_tm = std::gmtime(&expiry);
        char date_str[16];
        std::strftime(date_str, sizeof(date_str), "%Y%m%d", expiry_tm);
        
        // Compute checksum
        std::string data = customer_id + hardware_id + std::string(date_str);
        std::string checksum = compute_checksum(data);
        
        // Format license key
        return customer_id + "-" + hardware_id + "-" + std::string(date_str) + "-" + checksum;
    }
    
    /**
     * Create trial license (30 days, single channel)
     */
    static LicenseInfo create_trial_license() {
        LicenseInfo info;
        info.customer_id = "TRIAL";
        info.hardware_id = get_hardware_id();
        info.is_trial = true;
        info.max_channels = 1;
        
        // 30 days from now
        std::time_t now = std::time(nullptr);
        info.expiration_date = now + (30 * 24 * 60 * 60);
        
        return info;
    }
    
    /**
     * Load license from file
     */
    static LicenseStatus load_license_file(const std::string& filename, LicenseInfo& info) {
        FILE* f = fopen(filename.c_str(), "r");
        if (!f) {
            return LicenseStatus::NOT_FOUND;
        }
        
        char key[256];
        if (fgets(key, sizeof(key), f) == nullptr) {
            fclose(f);
            return LicenseStatus::INVALID_KEY;
        }
        fclose(f);
        
        // Remove newline
        size_t len = strlen(key);
        if (len > 0 && key[len-1] == '\n') {
            key[len-1] = '\0';
        }
        
        return validate_license(std::string(key), info);
    }
    
    /**
     * Get license status message
     */
    static std::string get_status_message(LicenseStatus status) {
        switch (status) {
            case LicenseStatus::VALID:
                return "License valid";
            case LicenseStatus::INVALID_KEY:
                return "Invalid license key format";
            case LicenseStatus::HARDWARE_MISMATCH:
                return "License not valid for this hardware";
            case LicenseStatus::EXPIRED:
                return "License has expired";
            case LicenseStatus::NOT_FOUND:
                return "License file not found";
            case LicenseStatus::TAMPERED:
                return "License file has been tampered with";
            case LicenseStatus::TRIAL_EXPIRED:
                return "Trial period has expired";
            default:
                return "Unknown license status";
        }
    }

private:
    /**
     * Simple checksum for license validation (8 hex digits)
     * Uses XOR and bit rotation for obfuscation
     */
    static std::string compute_checksum(const std::string& data) {
        uint32_t hash = 0x5A5A5A5A;  // Seed
        
        for (char c : data) {
            hash ^= static_cast<uint32_t>(c);
            hash = (hash << 7) | (hash >> 25);  // Rotate left 7 bits
            hash ^= 0x12345678;
        }
        
        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << hash;
        return ss.str();
    }
    
    /**
     * Parse date string YYYYMMDD to time_t
     */
    static std::time_t parse_date(const std::string& date_str) {
        if (date_str.length() != 8) {
            return 0;
        }
        
        std::tm tm = {};
        tm.tm_year = std::stoi(date_str.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(date_str.substr(4, 2)) - 1;
        tm.tm_mday = std::stoi(date_str.substr(6, 2));
        
        return std::mktime(&tm);
    }
    
    /**
     * Split string by delimiter
     */
    static std::vector<std::string> split_string(const std::string& str, char delim) {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        
        while (std::getline(ss, item, delim)) {
            result.push_back(item);
        }
        
        return result;
    }
};

} // namespace m110a

#endif // M110A_LICENSE_H
