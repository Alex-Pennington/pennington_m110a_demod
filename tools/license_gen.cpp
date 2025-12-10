/**
 * License Key Generator Tool
 * 
 * M110A Modem - MIL-STD-188-110A Compatible HF Modem
 * Copyright (c) 2024-2025 Alex Pennington
 * Email: alex.pennington@organicengineer.com
 * 
 * Admin utility to generate license keys for customers
 * Usage: license_gen.exe <customer_id> <hardware_id> [days_valid]
 */

#include "common/license.h"
#include <iostream>
#include <cstdlib>

using namespace m110a;

void print_usage() {
    std::cout << "M110A Modem License Key Generator\n";
    std::cout << "Copyright (c) 2024-2025 Alex Pennington\n";
    std::cout << "alex.pennington@organicengineer.com\n";
    std::cout << "==================================\n\n";
    std::cout << "Usage:\n";
    std::cout << "  license_gen.exe <customer_id> <hardware_id> [days_valid]\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  customer_id  - Customer identifier (e.g., ACME01)\n";
    std::cout << "  hardware_id  - Hardware fingerprint from customer\n";
    std::cout << "  days_valid   - Days until expiration (default: 365)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  license_gen.exe ACME01 A3B4C5D6 365\n";
    std::cout << "  license_gen.exe TRIAL A3B4C5D6 30\n\n";
    std::cout << "Get Hardware ID:\n";
    std::cout << "  license_gen.exe --hwid\n";
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--hwid") {
        // Display current hardware ID
        std::string hw_id = LicenseManager::get_hardware_id();
        std::cout << "Hardware ID: " << hw_id << "\n";
        std::cout << "\nGo to https://www.organicengineer.com/projects to obtain a license key using this ID.\n";
        return 0;
    }
    
    if (argc < 3) {
        print_usage();
        return 1;
    }
    
    std::string customer_id = argv[1];
    std::string hardware_id = argv[2];
    int days_valid = 365;
    
    if (argc >= 4) {
        days_valid = std::atoi(argv[3]);
    }
    
    // Validate inputs
    if (customer_id.empty() || hardware_id.empty()) {
        std::cerr << "Error: Customer ID and Hardware ID cannot be empty\n";
        return 1;
    }
    
    if (days_valid <= 0 || days_valid > 3650) {
        std::cerr << "Error: Days valid must be between 1 and 3650 (10 years)\n";
        return 1;
    }
    
    // Generate license key
    std::string license_key = LicenseManager::generate_license_key(
        customer_id, hardware_id, days_valid);
    
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  M110A Modem License Key Generated\n";
    std::cout << "========================================\n\n";
    std::cout << "Customer ID:  " << customer_id << "\n";
    std::cout << "Hardware ID:  " << hardware_id << "\n";
    std::cout << "Valid for:    " << days_valid << " days\n\n";
    std::cout << "LICENSE KEY:\n";
    std::cout << license_key << "\n\n";
    std::cout << "Save this key to 'license.key' file\n";
    std::cout << "========================================\n\n";
    
    // Verify the key
    LicenseInfo info;
    LicenseStatus status = LicenseManager::validate_license(license_key, info);
    
    if (status == LicenseStatus::HARDWARE_MISMATCH) {
        std::cout << "Note: License is for different hardware (expected)\n";
    } else if (status != LicenseStatus::VALID) {
        std::cerr << "Warning: Generated key validation failed: " 
                  << LicenseManager::get_status_message(status) << "\n";
        return 1;
    }
    
    return 0;
}
