# Version Iteration Instructions for Copilot

## Purpose
These instructions guide Copilot in automatically iterating version numbers and maintaining test reports across sessions.

---

## Version Format

```
MAJOR.MINOR.PATCH
```

- **MAJOR**: Breaking API changes or major architectural changes
- **MINOR**: New features, significant improvements, or merged developer updates
- **PATCH**: Bug fixes, minor improvements, test additions

---

## Current Version Tracking

| Component | Current Version | Git Commit | Git Tag | Last Updated |
|-----------|-----------------|------------|---------|---------------|
| API | 1.0.0 | 591900b | v1.0.0 | 2025-12-07 |
| Test Suite | 1.0.0 | 591900b | v1.0.0 | 2025-12-07 |

---

## When to Increment Versions

### Increment PATCH (1.0.0 → 1.0.1)
- Bug fix in existing code
- Minor test improvements
- Documentation updates
- Build system fixes

### Increment MINOR (1.0.0 → 1.1.0)
- New feature added
- Merged developer update folder (e.g., "6 MLSE Bugs Fixed")
- New test category added
- Performance improvement
- New mode support

### Increment MAJOR (1.0.0 → 2.0.0)
- Breaking API change
- Major architectural refactor
- Fundamental algorithm change

---

## Test Report Naming Convention

```
exhaustive_test_report_YYYY-MM-DD_vX.Y.Z.md
```

Example: `exhaustive_test_report_2025-12-07_v1.0.0.md`

---

## Git Version Tracking

### Git Commands for Version Management
```powershell
# Get current commit hash
git rev-parse --short HEAD

# Get full commit hash
git rev-parse HEAD

# Create version tag
git tag vX.Y.Z

# List all tags
git tag -l

# Show commit info for a tag
git show vX.Y.Z

# Commit with version message
git add -A
git commit -m "vX.Y.Z - Description of changes"
git tag vX.Y.Z
```

---

## Copilot Instructions for Version Iteration

### Before Starting Work
1. Check the latest report in `docs/test_reports/`
2. Note the current version number and git commit
3. Determine if changes warrant MAJOR, MINOR, or PATCH increment
4. Run `git log -1 --oneline` to verify current state

### After Making Changes
1. Run the exhaustive test suite:
   ```powershell
   cd d:\pennington_m110a_demod\test
   .\exhaustive_test.exe
   ```

2. Commit changes with version:
   ```powershell
   git add -A
   git commit -m "vX.Y.Z - Description of changes"
   git tag vX.Y.Z
   ```

3. Get new commit hash:
   ```powershell
   git rev-parse --short HEAD
   ```

4. Create new report with incremented version:
   ```
   docs/test_reports/exhaustive_test_report_YYYY-MM-DD_vX.Y.Z.md
   ```

5. Update this file's "Current Version Tracking" table with new commit hash

### Report Template Fields to Update
- **Date**: Current date (YYYY-MM-DD format)
- **Version**: Incremented version number
- **Git Commit**: From `git rev-parse --short HEAD`
- **Git Tag**: Version tag (e.g., v1.0.1)
- **Duration**: From test output
- **Iterations**: From test output
- **Total Tests**: From test output
- **All category results**: From test output

---

## Developer Update Folder Naming

Developer provides updates in numbered folders:
```
1 watterson_fix/
2 dfe_upgrade/
3. MLSE Integration Complete/
4 Equalizers - Phase Tracking/
5 DFE Preamble Pretraining/
6 MLSE Bugs Fixed/
7 EOM/
8 adaptive/
```

### Merge Process
1. Check contents: `Get-ChildItem "X folder_name/m110a_demod/"`
2. Compare with current: `git diff --no-index "current_file" "new_file"`
3. Copy updated files: `Copy-Item "source" -Destination "target" -Force`
4. Rebuild and test
5. Increment version (usually MINOR for developer updates)
6. Generate new test report

---

## Automated Version Bump Commands

### For Copilot to execute after changes:

```powershell
# Get current date
$date = Get-Date -Format "yyyy-MM-dd"

# After running tests, create report with new version
# Example for v1.1.0:
$version = "1.1.0"
$reportPath = "d:\pennington_m110a_demod\docs\test_reports\exhaustive_test_report_${date}_v${version}.md"
```

---

## Change Categories for Changelog

Use these categories in report changelog:

- **Added**: New features or capabilities
- **Changed**: Changes to existing functionality
- **Fixed**: Bug fixes
- **Improved**: Performance or quality improvements
- **Deprecated**: Features to be removed in future
- **Removed**: Removed features
- **Security**: Security-related changes

---

## Example Changelog Entry

```markdown
### v1.1.0 (2025-12-08)
- **Added**: Frequency offset compensation in phase tracker
- **Fixed**: M75 Walsh mode decoding edge cases
- **Improved**: DFE convergence speed on preamble
```

---

## Quick Reference: Common Scenarios

| Scenario | Version Change | Example |
|----------|----------------|---------|
| Merged folder "7 EOM" | MINOR | 1.0.0 → 1.1.0 |
| Fixed freq offset bug | PATCH | 1.1.0 → 1.1.1 |
| Added M75 improvements | PATCH | 1.1.1 → 1.1.2 |
| Major DFE rewrite | MINOR | 1.1.2 → 1.2.0 |
| API signature change | MAJOR | 1.2.0 → 2.0.0 |

---

## Copilot Session Startup Checklist

1. [ ] Check current version in this file
2. [ ] List any new developer update folders
3. [ ] Review TODO.md for pending work
4. [ ] Check last test report for known issues
5. [ ] Confirm build environment works: `g++ --version`

---

## Files to Keep Updated

| File | Update When |
|------|-------------|
| `docs/VERSION_ITERATION_INSTRUCTIONS.md` | Version changes |
| `docs/test_reports/exhaustive_test_report_*.md` | After test runs |
| `docs/TODO.md` | Tasks complete/added |
| `docs/PROJECT_STATUS.md` | Major milestones |
| `api/modem.h` (VERSION constants) | API version changes |

---

## API Version Constants Location

In `api/modem.h`:
```cpp
constexpr int VERSION_MAJOR = 1;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 0;
```

Update these when incrementing API version.
