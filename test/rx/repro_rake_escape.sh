#!/bin/bash
# repro_rake_escape.sh - the four AWGN -2 dB inputs that c1ee4ca decodes as whole wrong
# messages with success=1 (300L seeds 144, 208, 211; 150L seed 208), and the same inputs
# as gale-rx recordings. docs/m110a-acq-fading.md, "600L at the knee", has the context.
#
# usage: test/rx/repro_rake_escape.sh BIN_DIR OUT_DIR
#   BIN_DIR  a Release build of this checkout holding m110a_rx and modem_tx_wav
#            (cmake --build B --target m110a_rx modem_tx_wav; this branch's code is c1ee4ca's)
#   OUT_DIR  created; gets per input:
#            <tag>.payload.bin  the 200 bytes sent
#            <tag>.cs16         the I/Q m110a_rx decodes (2 MS/s, dial at +100 kHz)
#            <tag>.wav/.flac    48 kHz mono S16: m110a_rx's own 48 kHz audio of that I/Q
#                               (--dump-audio), at 3000 RMS, 0.5 s of noise before the signal;
#                               the .flac (with a "closed" .json sidecar) is gale-rx's input form
# Needs python3 with numpy and scipy; flac or ffmpeg for the .flac. No hfchansim: AWGN only.
#
# Rig, per input (as measured): payload = numpy default_rng([seed, mode index]).integers(0,
# 256, 200) with mode index 5 for 300L and 3 for 150L (the index in MODES below);
# modem_tx_wav --mode M --lead 0 --tail 0; the WAV's data as 48 kHz S16 PCM;
# synth_iq.py --snr -2 --seed seed --lead 0.5 --tail 0 --offset 100000 (AWGN in 3 kHz);
# m110a_rx --offset 100000 (AUTO, DFE).
# It prints each input's verdict twice: on the I/Q, as measured, and through gale-rx's audio
# path (48 kHz -> 8 kHz as to_8k_mono, cut 0.1 s before the signal, then m110a_check.run_rx:
# analytic, 96 kHz cf32 at +10 kHz, m110a_rx --format cf32 --rate 96000 --offset 10000 --dial 0).
# The second is a model of gale-rx's call, not gale-rx itself: run gale-rx decode on the .flac
# for its own verdict.
set -euo pipefail
[ $# -eq 2 ] || { sed -n '2,12p' "$0" >&2; exit 2; }
BIN=$(cd "$1" && pwd); OUT=$2
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
for b in m110a_rx modem_tx_wav; do [ -x "$BIN/$b" ] || { echo "no $BIN/$b" >&2; exit 2; }; done

verdict() {  # the first message's verdict, and its bytes against the payload sent ($1)
    python3 -I -c '
import re, sys
pay = open(sys.argv[1], "rb").read()
out = sys.stdin.read()
m = re.search(r"^msg=.*$", out, re.M)
if not m:
    print("no message"); sys.exit(0)
kv = dict(re.findall(r"(\w+)=(\"[^\"]*\"|\S+)", m.group(0)))
h = re.search(r"^  hex=(\S*)", out[m.end():], re.M)
b = bytes.fromhex(h.group(1)) if h else b""
wrong = sum(x != y for x, y in zip(b, pay)) + abs(len(b) - len(pay))
print(" ".join(k + "=" + kv.get(k, "?") for k in ("mode", "success", "quality_db", "block_quality_db",
      "block_quality_min_db", "block_quality_threshold_db", "eom", "bytes")),
      "bytes_differing_from_payload=%d" % wrong)
' "$1"
}

for case in 300L:144 300L:208 300L:211 150L:208; do
    mode=${case%%:*}; seed=${case##*:}; tag=${mode}_awgn_-2_${seed}; p=$OUT/$tag
    python3 -I - "$mode" "$seed" "$p.payload.bin" <<'EOF'
import sys
import numpy as np
MODES = ['75S', '75L', '150S', '150L', '300S', '300L', '600S', '600L', '1200S', '1200L', '2400S', '2400L', '4800S']
mode, seed, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
open(out, 'wb').write(np.random.default_rng([seed, MODES.index(mode)]).integers(0, 256, 200, dtype=np.uint8).tobytes())
EOF
    "$BIN/modem_tx_wav" --mode "$mode" --lead 0 --tail 0 "$p.payload.bin" "$p.tx.wav" >/dev/null
    python3 -I -c "import sys; r=open(sys.argv[1],'rb').read(); i=r.find(b'data'); open(sys.argv[2],'wb').write(r[i+8:])" \
        "$p.tx.wav" "$p.tx.pcm"
    python3 -I "$HERE/synth_iq.py" "$p.tx.pcm" "$p.cs16" --snr -2 --seed "$seed" --lead 0.5 --tail 0 \
        --offset 100000 2>/dev/null
    iq=$("$BIN/m110a_rx" --offset 100000 --dump-audio "$p.f32" "$p.cs16" 2>/dev/null | verdict "$p.payload.bin" || true)
    python3 -I - "$p" <<'EOF'
import json, os, sys
import numpy as np
from scipy.io import wavfile
p = sys.argv[1]
x = np.fromfile(p + '.f32', dtype='<f4').astype(np.float64)
x *= 3000.0 / np.sqrt(np.mean(x * x))
wavfile.write(p + '.wav', 48000, np.clip(np.round(x), -32768, 32767).astype('<i2'))
with open(p + '.json', 'w') as f:
    json.dump({'closed': True}, f)
os.remove(p + '.f32')
EOF
    if command -v flac >/dev/null; then flac -s -f -o "$p.flac" "$p.wav"
    elif command -v ffmpeg >/dev/null; then ffmpeg -loglevel error -y -i "$p.wav" "$p.flac"
    else echo "  (no flac or ffmpeg: $tag.flac not written)" >&2; fi
    python3 -I - "$p" <<'EOF'
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import hilbert, resample_poly
p = sys.argv[1]
sr, x = wavfile.read(p + '.wav')
y = np.clip(np.round(resample_poly(x.astype(float), 1, 6)), -32768, 32767).astype('<i2')  # to_8k_mono
y = y[int(0.4 * 8000):]                                     # the cut, 0.1 s before the signal
z = resample_poly(hilbert(y.astype(float) / 32768), 96000 // 8000, 1)
z *= np.exp(2j * np.pi * 10000.0 * np.arange(len(z)) / 96000)
np.stack([z.real, z.imag], 1).astype('<f4').tofile(p + '.gale.cf32')
EOF
    gale=$("$BIN/m110a_rx" --format cf32 --rate 96000 --offset 10000.0 --dial 0 "$p.gale.cf32" 2>/dev/null | verdict "$p.payload.bin" || true)
    rm -f "$p.tx.wav" "$p.tx.pcm" "$p.gale.cf32"
    echo "$tag  seed=$seed snr=-2"
    echo "  I/Q (as measured):        ${iq:-no message}"
    echo "  gale-rx audio path model: ${gale:-no message}"
done
