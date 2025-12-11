#pragma once
/**
 * @file html_content.h
 * @brief Combined HTML/CSS/JS for M110A Test GUI
 * 
 * This file combines modular components:
 * - html_common.h     - Shared CSS and utilities
 * - html_tab_tests.h  - Run Tests tab (PhoenixNest + Brain)
 * - html_tab_interop.h - Cross-Modem Interop tab
 * - html_tab_reports.h - Reports tab
 * - html_tab_melpe.h  - MELPe Vocoder tab
 */

#include "html_common.h"
#include "html_tab_tests.h"
#include "html_tab_interop.h"
#include "html_tab_reports.h"
#include "html_tab_melpe.h"

#include <string>
#include <sstream>

namespace test_gui {

// Build the complete HTML page from modular components
inline std::string build_html_page() {
    std::ostringstream html;
    
    html << R"HTML(<!DOCTYPE html>
<html>
<head>
    <title>M110A Modem Test Suite</title>
    <style>
)HTML";

    // Include CSS from modules
    html << HTML_CSS;           // Common styles
    html << HTML_CSS_INTEROP;   // Interop styles
    html << HTML_CSS_REPORTS;   // Reports styles
    html << HTML_CSS_MELPE;     // MELPe styles

    html << R"HTML(
    </style>
</head>
<body>
    <div class="container">
        <h1>M110A Modem Test Suite</h1>
        
        <div class="tabs">
            <button class="tab active" onclick="showTab('tests')">Run Tests</button>
            <button class="tab" onclick="showTab('interop')">Cross-Modem Interop</button>
            <button class="tab" onclick="showTab('reports')">Reports</button>
            <button class="tab" onclick="showTab('melpe')">MELPe Vocoder</button>
        </div>
)HTML";

    // Include tab content from modules
    html << HTML_TAB_TESTS;
    html << HTML_TAB_INTEROP;
    html << HTML_TAB_REPORTS;
    html << HTML_TAB_MELPE;

    html << R"HTML(
    </div>
    
    <script>
)HTML";

    // Include JavaScript from modules
    html << HTML_JS_COMMON;
    html << HTML_JS_TESTS;
    html << HTML_JS_INTEROP;
    html << HTML_JS_REPORTS;
    html << HTML_JS_MELPE;

    // Tab navigation and initialization
    html << R"JS(
        // Tab navigation
        function showTab(name) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            document.querySelector('.tab[onclick*="' + name + '"]').classList.add('active');
            document.getElementById('tab-' + name).classList.add('active');
            if (name === 'reports') loadReports();
            if (name === 'melpe') loadMelpeFiles();
        }
        
        // Initialize on load
        document.addEventListener('DOMContentLoaded', function() {
            initInteropMatrix();
            updateRateBadge();
        });
)JS";

    html << R"HTML(
    </script>
</body>
</html>
)HTML";

    return html.str();
}

// For backward compatibility, provide static HTML_PAGE
// Note: This is now dynamically generated
inline const char* get_html_page() {
    static std::string html_cache;
    if (html_cache.empty()) {
        html_cache = build_html_page();
    }
    return html_cache.c_str();
}

// Macro for easy access in server.h
#define HTML_PAGE test_gui::get_html_page()

} // namespace test_gui
