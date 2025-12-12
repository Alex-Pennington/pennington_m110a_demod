#pragma once
/**
 * @file html_tab_reports.h
 * @brief Reports tab HTML/JS for M110A Test GUI
 */

namespace test_gui {

// ============================================================
// REPORTS TAB CSS
// ============================================================
const char* HTML_CSS_REPORTS = R"CSS(
        /* Reports */
        .report-item { background: #0f3460; padding: 15px; border-radius: 8px; margin-bottom: 10px; display: flex; justify-content: space-between; align-items: center; }
        .report-name { color: #00d4ff; font-weight: bold; }
        .report-date { color: #888; font-size: 12px; }
        .report-link { color: #00d4ff; text-decoration: none; }
        .report-link:hover { text-decoration: underline; }
)CSS";

// ============================================================
// REPORTS TAB HTML
// ============================================================
const char* HTML_TAB_REPORTS = R"HTML(
        <!-- ============ REPORTS TAB ============ -->
        <div id="tab-reports" class="tab-content">
            <h2>Test Reports</h2>
            <button class="btn btn-secondary" onclick="loadReports()">🔄 Refresh</button>
            <div id="reports-list" style="margin-top:20px;">Loading...</div>
        </div>
)HTML";

// ============================================================
// REPORTS TAB JAVASCRIPT
// ============================================================
const char* HTML_JS_REPORTS = R"JS(
        async function loadReports() {
            const container = document.getElementById('reports-list');
            try {
                const r = await fetch('/list-reports');
                const d = await r.json();
                if (d.reports && d.reports.length > 0) {
                    container.innerHTML = d.reports.map(rep => 
                        '<div class="report-item"><div><span class="report-name">' + rep.name + '</span><br><span class="report-date">' + rep.date + ' | ' + rep.size + '</span></div><a href="/report/' + encodeURIComponent(rep.name) + '" class="report-link" target="_blank">View →</a></div>'
                    ).join('');
                } else {
                    container.innerHTML = '<p style="color:#888;">No reports found.</p>';
                }
            } catch (e) {
                container.innerHTML = '<p style="color:#ff4757;">Error: ' + e.message + '</p>';
            }
        }
)JS";

} // namespace test_gui
