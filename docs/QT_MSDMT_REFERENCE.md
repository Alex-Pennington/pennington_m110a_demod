# Qt MSDMT Reference - De Facto Standard for Brain Core Interoperability

## Overview

This document captures the de facto standard implementation of the Brain Core (m188110a) 
modem interface as implemented in Qt MSDMT. Since Brain Core is a **sealed library**, 
all interoperability must conform to how Qt MSDMT interfaces with it.

**Purpose**: Sanity check our `brain_tcp_server.h` wrapper against the known-working Qt MSDMT implementation.

---

## 1. Architecture Summary

### Qt MSDMT Structure
```
ms-dmt-backend/
├── modemservice.cpp     - Core modem interface (TX/RX management)
├── modemservice.h       - Modem service header
├── tcpserver.cpp        - TCP server implementation
├── tcpserver.h          - TCP protocol: data:4998, control:4999
├── audiomanager.cpp     - Audio I/O handling
├── configmanager.cpp    - Configuration management
└── bridge.cpp           - Optional bridging
```

### Key Finding: Separate RX and TX Modem Instances
**CRITICAL**: Qt MSDMT uses **TWO separate Cm110s instances**:
- `m_rxModem` - for receiving
- `m_txModem` - for transmitting

From `modemservice.cpp:139-140`:
```cpp
m_rxModem = new Cm110s();
m_txModem = new Cm110s();
```

**Our Implementation**: Uses single `brain::Modem` wrapper which internally has one `Cm110s`.

---

## 2. Modem Initialization Parameters

### TX Modem Initialization (modemservice.cpp:158-167)
```cpp
m_txModem->tx_set_soundblock_size(1920);
m_txModem->register_status(g_statusCallback);
m_txModem->tx_enable();
m_txModem->rx_enable();  // NOTE: Even TX modem has RX enabled!
m_txModem->set_psk_carrier(1800);
m_txModem->set_preamble_hunt_squelch(8);  // Value 8 = "None"
m_txModem->set_p_mode(1);
m_txModem->set_e_mode(0);
m_txModem->set_b_mode(0);
m_txModem->m_eomreset = 0;
m_txModem->eom_rx_reset();
```

### RX Modem Initialization (modemservice.cpp:170-175)
```cpp
m_rxModem->rx_enable();
m_rxModem->set_psk_carrier(1800);
m_rxModem->set_preamble_hunt_squelch(8);  // Value 8 = "None"
m_rxModem->set_p_mode(1);
m_rxModem->set_e_mode(0);
m_rxModem->set_b_mode(0);
```

### Our Implementation Status
| Parameter | Qt MSDMT | Our brain_wrapper.h | Match? |
|-----------|----------|---------------------|--------|
| tx_set_soundblock_size | 1920 | 1024 | ❌ NO |
| set_psk_carrier | 1800 Hz | Not set | ❌ NO |
| set_preamble_hunt_squelch | 8 | Not set | ❌ NO |
| set_p_mode | 1 | Not set | ❌ NO |
| set_e_mode | 0 | Not set | ❌ NO |
| set_b_mode | 0 | Not set | ❌ NO |
| m_eomreset | 0 | Not set | ❌ NO |
| eom_rx_reset() | Called | Not called | ❌ NO |
| Separate RX/TX modems | Yes | No (single) | ❌ NO |

---

## 3. Audio Processing

### Sample Rates
- **Modem Internal**: 9600 Hz (float samples)
- **Audio System**: 48000 Hz (int16 samples)
- **Conversion**: 5:1 interpolation/decimation

### TX Audio Block Size
From `modemservice.cpp:389`:
```cpp
const int MODEM_BLOCK_SIZE = 1920;  // 9.6kHz samples
```

This is 200ms of audio at 9.6kHz (1920/9600 = 0.2s).

### RX Audio Processing (modemservice.cpp:284-291)
```cpp
void ModemService::onAudioSamplesReady(const QByteArray &samples)
{
    if (m_rxModem) {
        const int16_t *data = reinterpret_cast<const int16_t*>(samples.constData());
        int numSamples = samples.size() / sizeof(int16_t);
        m_rxModem->rx_process_block(const_cast<int16_t*>(data), numSamples);
    }
}
```

**Key Finding**: Qt MSDMT processes audio as `int16_t` samples, not `float`.

---

## 4. Callback Registration

### Qt MSDMT Pattern (modemservice.cpp:14-26)
```cpp
static ModemService *g_modemService = nullptr;

static void g_rxOctetCallback(unsigned char octet)
{
    if (g_modemService) {
        g_modemService->handleRxOctet(octet);
    }
}

static void g_statusCallback(ModemStatus status, void *param)
{
    if (g_modemService) {
        g_modemService->handleStatusChange(status, param);
    }
}
```

Then in setup:
```cpp
m_rxModem->register_receive_octet_callback_function(g_rxOctetCallback);
m_rxModem->register_status(g_statusCallback);
```

---

## 5. Status Callbacks

### ModemStatus Values Handled (modemservice.cpp:209-276)
- `SNR_STATUS` - Updates current SNR value
- `DCD_TRUE_STATUS` - Sets state to "SYNC"
- `DCD_FALSE_STATUS` - Sets state to "IDLE", flushes RX buffer
- `TX_TRUE_STATUS` - Sets transmitting flag, sends "TX TRUE\n"
- `TX_FALSE_STATUS` - Clears transmitting, sends "TX FALSE\n", deasserts PTT
- `TRAIN_STATUS` - Sets state to "TRAIN"
- `TEXT_STATUS` - Emits text message

---

## 6. TCP Protocol

### Ports
| Port | Type | Purpose |
|------|------|---------|
| 4998 | TCP | Data port - message traffic in/out |
| 4999 | TCP | Control port - commands and status |
| 5000 | UDP | Broadcast - HELO/BYE announcements |
| 5834 | UDP | Inbound - CP responses |

### Control Commands
From tcpserver.h and modemservice.cpp, the control port accepts:
- Config commands (prefixed handling)
- Query commands 
- Control commands

### Status Messages Sent
- `TX TRUE\n` - When transmission starts
- `TX FALSE\n` - When transmission ends

---

## 7. Critical Differences with Our Implementation

### Issue 1: Single vs Dual Modem Instances
Qt MSDMT uses separate `Cm110s` instances for RX and TX. This might be significant for state management.

### Issue 2: Missing Initialization Parameters
Our `brain_wrapper.h` doesn't set:
- `set_psk_carrier(1800)` - Carrier frequency
- `set_preamble_hunt_squelch(8)` - Squelch setting
- `set_p_mode(1)` - Unknown mode flag
- `set_e_mode(0)` - Unknown mode flag
- `set_b_mode(0)` - Unknown mode flag
- Proper `tx_set_soundblock_size(1920)`

### Issue 3: Sample Block Size Mismatch
- Qt MSDMT: 1920 samples
- Our code: 1024 samples

### Issue 4: Audio Sample Type
- Qt MSDMT uses `int16_t` for `rx_process_block()`
- Our code also uses `int16_t` (correct)

---

## 8. Recommended Actions

1. **Update brain_wrapper.h initialization** to match Qt MSDMT parameters
2. **Consider dual modem instances** if single instance causes issues
3. **Change soundblock size** from 1024 to 1920
4. **Add missing set_* calls** for carrier, squelch, and mode flags

---

## 9. Source Files Referenced

- `excluded_from_git/Qt MSDMT Project/ms-dmt-backend/modemservice.cpp` (867 lines)
- `excluded_from_git/Qt MSDMT Project/ms-dmt-backend/modemservice.h` (125 lines)
- `excluded_from_git/Qt MSDMT Project/ms-dmt-backend/tcpserver.h` (103 lines)

---

*Document created: December 12, 2025*
*Purpose: Sanity check for brain_tcp_server.h interoperability*
