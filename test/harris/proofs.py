"""The T7 proof document for the Harris MELP test set (score.py --proofs DIR).

Writes DIR/t7.json and the audio it names under DIR/t7-audio/. Text fields carry no file
paths, host names or frequencies; check_text() is the test of that.
"""
import json
import os
import re

TITLE = "Harris radio MELP voice"
WHAT = ("Eight voice transmissions recorded from a Harris RT-1694D (AN/PRC-150): "
        "MELP speech sent over the serial-tone modem.")
AUDIO_DIR = "t7-audio"

# Read off each recording's preamble until the receiver reports them: D1,D2, the
# countdown start (2 = short or zero interleave) and the number of preambles found.
MODE_TABLE = {
    "600 #1": (5, 7, 2, 1),
    "600 #2": (5, 7, 2, 1),
    "600 #3 multiples": (5, 7, 2, 4),
    "1200 #1": (5, 6, 2, 1),
    "1200 #2": (5, 6, 2, 1),
    "1200 #3": (5, 6, 2, 1),
    "melp600": (5, 7, 2, 1),
    "melp2400": (7, 7, 2, 1),
}

TEXT_KEYS_DOC = ("id", "title", "what", "decoder", "note")
TEXT_KEYS_ITEM = ("name", "mode", "status", "decoded", "detail")

# a path: starts with / ~ ./ ../ or a drive, or has a directory part and a file extension
_PATH = re.compile(r"(^|[\s(\"'])(~|\.{1,2}/|/|[A-Za-z]:\\)\S*|\S+/\S+\.[A-Za-z0-9]{1,4}\b|\\\\")
_HOST = re.compile(r"\b[\w-]+\.(internal|local|lan|home|com|net|org|io)\b"
                   r"|\b\d{1,3}(\.\d{1,3}){3}\b"
                   r"|\b(saga|munin|voror|uror|farm\d|gangleri|brokkr)\b", re.I)
_FREQ = re.compile(r"\b\d+(\.\d+)?\s*(Hz|kHz|MHz|GHz)\b|\b(Hz|kHz|MHz|GHz)\b", re.I)


def check_text(doc):
    """Return a list of 'field: problem' strings; empty when the document is clean."""
    bad = []

    def one(where, v):
        if not isinstance(v, str):
            return
        for name, rx in (("path", _PATH), ("host", _HOST), ("frequency", _FREQ)):
            if rx.search(v):
                bad.append(f"{where}: {name} in {v!r}")

    for k in TEXT_KEYS_DOC:
        one(k, doc.get(k))
    for i, it in enumerate(doc.get("items", [])):
        for k in TEXT_KEYS_ITEM:
            one(f"items[{i}].{k}", it.get(k))
        for j, au in enumerate(it.get("audio", [])):
            one(f"items[{i}].audio[{j}].label", au.get("label"))
    return bad


def mode_text(name, meta):
    """What was read off the preamble, from the receiver's sidecar or MODE_TABLE."""
    d1, d2, count, npre = MODE_TABLE[name]
    d1 = meta.get("d1", d1)
    d2 = meta.get("d2", d2)
    count = meta.get("countdown", count)
    npre = meta.get("preambles", npre)
    il = {2: "short or zero interleave", 23: "long interleave"}.get(count, "interleave unknown")
    parts = [f"D1,D2 {d1},{d2}", f"count {count} ({il})"]
    if npre != 1:
        parts.append(f"{npre} preambles")
    if "rate" in meta:
        parts.append(f"{meta['rate']} bps")
    if "length_s" in meta:
        parts.append(f"{float(meta['length_s']):.1f} s")
    return ", ".join(parts)


def status_for(have_payload, verified, speech_ok):
    """pass: speech decoded and the measure says speech. partial: payload bits decoded and
    verified, speech not right. none: only the preamble and code read. fail: the rest."""
    if not have_payload:
        return "none"
    if speech_ok:
        return "pass"
    if verified:
        return "partial"
    return "fail"


def item(name, mode, status, decoded, detail=None, audio=()):
    it = {"name": name, "mode": mode, "status": status, "decoded": decoded}
    if detail:
        it["detail"] = detail
    it["audio"] = [dict(a) for a in audio]
    return it


def document(decoder, note, items):
    return {"id": "T7", "order": 50, "title": TITLE, "what": WHAT,
            "decoder": decoder, "note": note, "items": items}


def write(proofs_dir, doc):
    """Write t7.json after checking text and that every named audio file exists."""
    bad = check_text(doc)
    if bad:
        raise ValueError("proof text: " + "; ".join(bad))
    for it in doc["items"]:
        for au in it["audio"]:
            p = os.path.join(proofs_dir, au["file"])
            if not os.path.isfile(p):
                raise ValueError(f"{it['name']}: audio {au['file']} not written")
    tmp = os.path.join(proofs_dir, "t7.json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp, os.path.join(proofs_dir, "t7.json"))
