#ifndef M110A_DMT_MODEM_H
#define M110A_DMT_MODEM_H

/**
 * MIL-STD-188-110D Discrete Multi-Tone (DMT) Modem
 * 
 * Also known as MS-DMT or OFDM modem for HF channels.
 * 
 * Key parameters:
 *   - 39 data subcarriers (can be reduced for narrower channels)
 *   - 75 Hz subcarrier spacing
 *   - 13.33 ms symbol duration (1/75 Hz)
 *   - 2925 Hz occupied bandwidth (39 * 75 Hz)
 *   - Center frequency: 1800 Hz (standard)
 *   - Subcarrier range: 337.5 Hz to 3262.5 Hz
 *   - Cyclic prefix: 10.67 ms (for multipath tolerance)
 *   - Total OFDM symbol: 24 ms
 * 
 * Data rates (with rate 1/2 FEC):
 *   - QPSK:  3200 b