"""Sweep m110a_rx over synthetic RSP2 I/Q: modes x SNR x mistuning x start offset x seeds.

usage: python3 -I rx_sweep.py --rx PATH/m110a_rx --ref DIR [--modes 600S,150S]
           [--snr 20] [--foff 0] [--lead 0.1] [--seeds 1] [--ppm-dial HZ:PPM]
           [--work DIR] [--jobs N] [--min-pass N] [--auto] [--stdin] [--tail S]

For every case: synth_iq.py writes the I/Q (dial at +100 kHz, plus foff Hz
of mistuning), m110a_rx decodes it with --expect set to the reference
payload, and a row "mode snr foff lead seed PASS|FAIL acq_foff start_s" is
printed. With --ppm-dial F:P the I/Q dial is put where a receiver whose
clock is off by P ppm sees it (offset - F*P*1e-6) and m110a_rx is given
--dial F --ppm P. Exit status 0 when at least --min-pass cases pass (default:
all of them).
"""
import argparse
import concurrent.futures as cf
import glob
import os
import re
import subprocess
import sys
import tempfile

PAYLOAD = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890"
HERE = os.path.dirname(os.path.abspath(__file__))

ap = argparse.ArgumentParser()
ap.add_argument('--rx', required=True)
ap.add_argument('--ref', required=True)
ap.add_argument('--modes', default='600S')
ap.add_argument('--snr', default='20')
ap.add_argument('--foff', default='0')
ap.add_argument('--lead', default='0.1')
ap.add_argument('--seeds', default='1')
ap.add_argument('--ppm-dial', default='')
ap.add_argument('--auto', action='store_true', help='let m110a_rx detect the mode (AUTO)')
ap.add_argument('--tail', default='0.3')
ap.add_argument('--stdin', action='store_true', help='feed m110a_rx through stdin')
ap.add_argument('--work', default='')
ap.add_argument('--jobs', type=int, default=2)
ap.add_argument('--min-pass', type=int, default=-1)
a = ap.parse_args()

floats = lambda s: [float(v) for v in s.split(',')]
work = a.work or tempfile.mkdtemp(prefix='rx_sweep.', dir=os.getcwd())
os.makedirs(work, exist_ok=True)
dial = ppm = None
if a.ppm_dial:
    dial, ppm = (float(v) for v in a.ppm_dial.split(':'))


def run(case):
    mode, snr, foff, lead, seed = case
    ref = glob.glob(os.path.join(a.ref, f'tx_{mode}_*.pcm'))
    if not ref:
        return case, 'FAIL', 'no-ref', ''
    tag = f'{mode}_{snr:g}_{foff:g}_{lead:g}_{seed}'
    iq = os.path.join(work, tag + '.cs16')
    offset = 100000.0 - (dial * ppm * 1e-6 if dial else 0.0)
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'synth_iq.py'), ref[0], iq,
                    '--offset', repr(offset), '--snr', repr(snr), '--seed', str(seed),
                    '--foff', repr(foff), '--lead', repr(lead), '--tail', a.tail],
                   check=True, stderr=subprocess.DEVNULL)
    cmd = [a.rx, '--offset', '100000', '--expect', PAYLOAD]
    if not a.auto:
        cmd += ['--mode', mode]
    if dial:
        cmd += ['--dial', repr(dial), '--ppm', repr(ppm)]
    if a.stdin:
        with open(iq, 'rb') as f:
            p = subprocess.run(cmd + ['--rate', '2000000', '-'], stdin=f, capture_output=True, text=True)
    else:
        p = subprocess.run(cmd + [iq], capture_output=True, text=True)
    os.remove(iq)
    acq, start = '', ''
    for line in p.stdout.splitlines():
        if line.startswith('msg=') and (not acq or 'eom=1' in line):
            m = re.search(r'acq_foff_hz=(\S+)', line)
            acq = m.group(1) if m else ''
            m = re.search(r'start_s=(\S+)', line)
            start = m.group(1) if m else ''
    return case, 'PASS' if p.returncode == 0 else 'FAIL', acq or '-', start or '-'


cases = [(m, s, f, l, int(e)) for m in a.modes.split(',') for s in floats(a.snr)
         for f in floats(a.foff) for l in floats(a.lead) for e in floats(a.seeds)]
passed = 0
with cf.ThreadPoolExecutor(max_workers=a.jobs) as ex:
    for (mode, snr, foff, lead, seed), res, acq, start in ex.map(run, cases):
        passed += res == 'PASS'
        print(f'{mode} {snr:g} {foff:+g} {lead:g} {seed} {res} {acq} {start}', flush=True)
need = len(cases) if a.min_pass < 0 else a.min_pass
print(f'# {passed}/{len(cases)} passed (need {need})', flush=True)
sys.exit(0 if passed >= need else 1)
