# M110A Exhaustive Test Suite

Modular test framework for comprehensive modem testing.

## File Structure

```
test/exhaustive/
├── main.cpp              - Entry point
├── cli.h                 - CLI parsing, Config struct
├── output.h              - IOutput interface + HumanOutput + JsonOutput
├── exhaustive_runner.h   - Main test loop
├── progressive_runner.h  - Progressive (find-limits) tests
└── README.md             - This file
```

## Dependencies

- `test_framework.h` - Shared test infrastructure
- `direct_backend.h` - Direct API backend
- `server_backend.h` - TCP server backend

## Building

```powershell
# From project root
.\build.ps1 -Target exhaustive

# Or manually
g++ -std=c++17 -O2 -I. -Isrc -o test/exhaustive_test.exe test/exhaustive/main.cpp -lws2_32
```

## Usage

### Basic Exhaustive Test
```bash
exhaustive_test.exe --duration 180           # Run for 3 minutes
exhaustive_test.exe --iterations 5           # Run 5 iterations
exhaustive_test.exe --mode 600S              # Test only 600S mode
```

### JSON Output (Machine Readable)
```bash
exhaustive_test.exe --json --duration 60     # JSON lines output
```

### Progressive Tests
```bash
exhaustive_test.exe --progressive            # Find all limits
exhaustive_test.exe --prog-snr              # SNR sensitivity only
exhaustive_test.exe --prog-freq             # Freq tolerance only
exhaustive_test.exe --prog-multipath        # Multipath tolerance only
```

### Backend Selection
```bash
exhaustive_test.exe                          # Direct API (default)
exhaustive_test.exe --server                 # TCP server backend
exhaustive_test.exe --server --host 192.168.1.100 --port 5000
```

### Parallelization
```bash
exhaustive_test.exe --parallel 4            # 4 threads (direct API only)
```

### Equalizer Selection
```bash
exhaustive_test.exe --eq DFE                # DFE only (default)
exhaustive_test.exe --eq MLSE               # MLSE only
exhaustive_test.exe --eq BOTH               # Test both
```

## JSON Output Format

When `--json` is specified, output is JSON lines (one JSON object per line):

```json
{"type":"start","backend":"Direct API","mode_detection":"KNOWN","equalizers":["DFE"],"duration_sec":180}
{"type":"test","elapsed":5,"mode":"600S","channel":"awgn_25db","tests":23,"passed":22,"rate":95.7,"result":"PASS","ber":0.000000,"iter":1,"max_iter":999999}
{"type":"mode_stats","mode":"600S","passed":10,"failed":1,"total":11,"rate":90.9,"avg_ber":0.001234}
{"type":"done","duration":180,"iterations":3,"tests":132,"passed":120,"failed":12,"rate":90.9,"avg_ber":0.001234,"rating":"GOOD"}
```

### Message Types

| Type | Description |
|------|-------------|
| `start` | Test session started |
| `test` | Individual test result |
| `progress` | Periodic progress update |
| `mode_stats` | Per-mode statistics |
| `channel_stats` | Per-channel statistics |
| `progressive` | Progressive test result |
| `done` | Test session complete |
| `info` | Informational message |
| `error` | Error message |

## Architecture

### IOutput Interface

The `IOutput` interface defines all output events. Two implementations:

- **HumanOutput**: Console-friendly format with progress bars
- **JsonOutput**: Machine-readable JSON lines

This allows easy integration with GUIs (parse JSON) while maintaining
human-readable console output.

### Runners

- **ExhaustiveRunner**: Runs all mode/channel combinations
- **ProgressiveRunner**: Binary search to find performance limits

### Config

Single `Config` struct holds all settings, parsed from CLI in `cli.h`.

## Adding New Output Formats

1. Create new class implementing `IOutput`
2. Add to factory in `output.h`
3. Add CLI flag in `cli.h`

Example for CSV output:
```cpp
class CsvOutput : public IOutput {
    void on_test_result(...) override {
        std::cout << mode << "," << channel << "," << (passed ? "PASS" : "FAIL") << "\n";
    }
    // ... other methods
};
```

## Integration with Test GUI

The test GUI can use `--json` mode to get structured output:

```cpp
FILE* pipe = _popen("exhaustive_test.exe --json --duration 60", "r");
char buffer[1024];
while (fgets(buffer, sizeof(buffer), pipe)) {
    // Parse JSON line
    auto json = parse_json(buffer);
    if (json["type"] == "test") {
        update_progress(json["tests"], json["rate"]);
    } else if (json["type"] == "done") {
        show_results(json);
    }
}
```
