# AF-DM Source Code Analysis
## Phoenix Nest Interoperability Investigation

### Overview
AF-DM (Air Force Digital Modem) v1.00.25 is a Qt6 GUI wrapper around Paul Brain's proprietary modem core library (Cm110s).
The modem core itself is not included in the repository.

---

## Key Technical Parameters

### Sample Rates
- **Internal modem rate**: 9600 Hz
- **Audio I/O rate**: 48000 Hz (5x interpolation/decimation)
- **Soundblock size**: 1920 samples @ 9600 Hz (200ms)

### Mode Enum (Index → Mode)
```
0  = 75S    (M75S)
1  = 75L    (M75L)
2  = 150S   (M150S)
3  = 150L   (M150L)
4  = 300S   (M300S)
5  = 300L   (M300L)
6  = 600S   (M600S)
7  = 600L   (M600L)   ← Default
8  = 1200S  (M1200S)
9  = 1200L  (M1200L)
10 = 2400S  (M2400S)
11 = 2400L  (M2400L)
12 = 4800U  (M4800U)  ← Not in Phoenix Nest
```

---

## Modem Core API (Cm110s class)

### TX Functions
- `tx_set_mode(Mode)` - Set transmit data rate
- `tx_sync_frame_eom(data, length)` - Generate TX frame with sync and EOM
- `tx_enable()` / `tx_disable()` - Enable/disable transmitter
- `tx_set_soundblock_size(int)` - Set output buffer size
- `tx_get_soundblock()` - Get audio samples from TX queue
- `tx_release_soundblock(float*)` - Release processed soundblock

### RX Functions
- `rx_enable()` / `rx_disable()` - Enable/disable receiver
- `rx_get_mode()` - Get detected mode (returns Mode enum)
- `rx_get_mode_string()` - Get mode as string
- `eom_rx_reset()` - Reset EOM detection
- `set_preamble_hunt_squelch(int)` - Set preamble detection threshold (8-10)

### Other Functions
- `set_psk_carrier(int)` - Set carrier frequency (1800 Hz)
- `set_p_mode(int)` / `set_e_mode(int)` / `set_b_mode(int)` - Mode settings
- `register_status(callback)` - Register status callback

---

## Status Callback Events (ModemStatus enum)
```cpp
TEXT_STATUS       - Decoded text available
DCD_FALSE_STATUS  - Data carrier lost
DCD_TRUE_STATUS   - Data carrier detected
TX_TRUE_STATUS    - Transmitter active
TX_FALSE_STATUS   - Transmitter idle
FREQ_ERROR_STATUS - Frequency error measurement
SNR_STATUS        - Signal-to-noise ratio
PREAM_SNR_STATUS  - Preamble SNR
VITERBI_STATUS    - Viterbi decoder status
TRAIN_STATUS      - Training sequence status
BER_STATUS        - Bit error rate
BEC_STATUS        - Block error count
```

---

## TCP Control Protocol

### Default Ports
- Control: 63024
- Data: 63023

### Commands (Control Port → Modem)
| Command | Action | Response |
|---------|--------|----------|
| `75S`, `75L`, `150S`... | Set data rate | `<mode>\n` |
| `SEND` | Transmit buffered data | `SENDING\n`, `IDLE\n` |
| `KILL` | Abort transmission | `KILLED\n` |
| `RESET` | Reset modem | `RESET\n` |
| `MD ON` / `MD OFF` | Enable/disable match data rate | `MD ON\n` / `MD OFF\n` |
| `MD?` | Query match data rate | `MD ON\n` or `MD OFF\n` |
| `DR?` | Query current data rate | `DR <mode>\n` |
| `VR?` | Query version | `Version: X.XX.XX\n` |

### Status Messages (Modem → Control Port)
| Message | Meaning |
|---------|---------|
| `DCD <mode>` | Data carrier detected (e.g., `DCD 600S`) |
| `IDLE` | Modem idle |
| `BUSY` | Transmitter active |
| `SENDING` | Transmission in progress |
| `ERROR` | Error condition |

### Data Flow
1. Send message bytes to Data Port
2. Send `SEND` command to Control Port
3. Modem responds `SENDING` → generates TX audio → `IDLE`
4. Receive: Data bytes appear on Data Port after `DCD <mode>` on Control Port

---

## Sync Modes
```cpp
typedef enum {
    SYNC_EOM_MODE,  // 0 - Default: Sync with End-of-Message detection
    ASYNC_MODE,     // 1 - Asynchronous (no sync)
    SYNC_MODE       // 2 - Sync without EOM
} SyncMode;
```

---

## Implications for Phoenix Nest

### What We Learned
1. **Sample rate confirmed**: 9600 Hz internal (Phoenix Nest uses this correctly)
2. **Mode indexing confirmed**: Same order as Phoenix Nest
3. **Carrier frequency**: 1800 Hz (standard)
4. **Soundblock size**: 1920 samples is the standard chunk size

### What's NOT in the Code
The actual preamble structure is in `Cm110s.cpp` (the proprietary core), which includes:
- Preamble symbol sequence
- D1/D2 marker positions
- Sync pattern timing
- Calibration burst format

### Key Difference Identified (from earlier analysis)
| Property | Brain TX | Phoenix Nest TX |
|----------|----------|-----------------|
| Symbol count | 3,360 | 3,072 |
| Difference | **288 symbols (6 miniframes)** |
| Start pattern | Calibration burst with AGC settling | Immediate full power |

### Fix Applied (Build 208)
Phoenix Nest TX was updated to add:
1. 288 leading symbols (calibration burst pattern)
2. Soft ramp-up envelope

### Remaining Verification
What blocked software testing:
AF-DM's RXAUDIOINJECT command didn't work. You couldn't feed PCM files to the decoder, so the only way to verify interop was over RF.
What's changed:
The brain_core repo now has a working headless server with:

CMD:RXAUDIOINJECT:<path> — feed PCM directly to RX
TX PCM capture to files
Full modem core source (not just the wrapper)

So now we can do Brain TX → PCM → Phoenix RX and vice versa, all in software. No radio required for the initial verification.

---

## File Structure Reference

```
AF-DM-v1.00.25/
├── Cm110s.h         [NOT PROVIDED - Proprietary modem core header]
├── globals.h        - Version info, sample block sizes
├── dint.cpp/h       - Decimation/Interpolation (48000 ↔ 9600)
├── bridge.cpp/h     - Qt signal bridge for threading
├── modemAudioOut.cpp/h - TX audio output handling
├── mainwindow.cpp/h - Main GUI and control logic
├── mainwindow.ui    - Qt Designer UI file
└── main.cpp         - Application entry point
```

---

## Author Information
From README:
> GUI code for AF-DM release version 1.00.25.
> The modem core is proprietary and will not be shared per agreement with author.
> Questions: afa4nq(at)comcast(dot)net with subject "AF-DM GUI Code"

The modem core appears to be licensed software with license expiration (reference to ALPHA_TIME, BETA_TIME in globals.h).
