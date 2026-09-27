#!/usr/bin/env python3
"""Read-only live probe for the Bully draw-distance investigation.

Attach this to a RUNNING, already-in-gameplay Bully.exe. It reads a handful of
globals and prints them when they change. It writes nothing to the process: no
Interceptor, no code patching, no allocation. That matters because Bully.exe is
SecuROM-packed and inline hooks are the thing most likely to upset it.

What it watches and why:

  0x00C13F00  visible-object list count, reset every frame. The array is 2000
              entries at 0x00C11FC0 and this count sits immediately after it,
              which is what makes 2000 a hard ceiling. The ASI's own watcher
              samples this once a second and therefore sees about 1 frame in 60;
              this polls at ~60 Hz so it can actually catch the peak.

  0x00BD1008  believed to be the current area id. Bully-DE's Diagnostics.cpp
              already calls it kAreaId, but IDA types the address as a float and
              nothing has ever confirmed which it is. Printed BOTH ways, so
              walking from the street into an interior settles it: if the int
              changes to a small stable number, it is the area id, and the
              "clamp draw distance indoors" fix has a signal to key off.

  0x00C3CD00  the global LOD multiplier Bully-DE writes (flt_C3CD00). Confirms
              the value the game is really using.

Usage:
    python tools/probe_drawdistance.py                 # until Ctrl+C
    python tools/probe_drawdistance.py --seconds 300
    python tools/probe_drawdistance.py --out probe.log

Addresses assume the image is based at 0x400000 and DECRYPTED, so attach once
you are in gameplay, not at the menu.
"""

import argparse
import datetime
import sys
import time

try:
    import frida
except ImportError:
    sys.exit("frida is not installed -- run: pip install frida-tools")

PROCESS = "Bully.exe"

AGENT = r"""
const ADDR = {
    listCount: ptr(0x00C13F00),   // visible-object list count (per frame)
    areaId:    ptr(0x00BD1008),   // believed current area id
    lodMult:   ptr(0x00C3CD00),   // global LOD multiplier
    lodHook:   ptr(0x004F3720),   // Bully-DE's camera LOD hook site
};
const LIST_CAP = 2000;

function u32(p) { try { return p.readU32(); } catch (e) { return null; } }
function f32(p) { try { return p.readFloat(); } catch (e) { return null; } }

function sanity() {
    let bytes = null;
    try { bytes = ADDR.lodHook.readByteArray(6); } catch (e) {}
    let state = "unknown";
    if (bytes !== null) {
        const b = new Uint8Array(bytes);
        if (b[0] === 0xE9) state = "Bully-DE hook present (image unpacked)";
        else if (b[0] === 0xD9 && b[1] === 0xE8) state = "vanilla bytes (unpacked, ASI not patched here)";
        else state = "unrecognised -- image may still be packed";
    }
    send({ kind: "sanity", state: state,
           lodMult: f32(ADDR.lodMult),
           areaInt: u32(ADDR.areaId),
           areaFloat: f32(ADDR.areaId) });
}

let peak = 0;
let lastArea = null;
let overCap = 0;
let samples = 0;

function tick() {
    samples++;
    const count = u32(ADDR.listCount);
    const areaInt = u32(ADDR.areaId);

    if (count !== null) {
        if (count > LIST_CAP) {
            overCap++;
        } else if (count > peak) {
            const jump = (count >= peak + 50) || (count >= LIST_CAP - 100);
            peak = count;
            if (jump) {
                send({ kind: "peak", peak: peak, cap: LIST_CAP,
                       pct: Math.round((peak * 100) / LIST_CAP),
                       areaInt: areaInt });
            }
        }
    }

    if (areaInt !== lastArea) {
        send({ kind: "area", from: lastArea, to: areaInt,
               areaFloat: f32(ADDR.areaId),
               listCount: count, peak: peak,
               lodMult: f32(ADDR.lodMult) });
        lastArea = areaInt;
    }
}

function heartbeat() {
    send({ kind: "beat", listCount: u32(ADDR.listCount), peak: peak,
           cap: LIST_CAP, areaInt: u32(ADDR.areaId),
           areaFloat: f32(ADDR.areaId), lodMult: f32(ADDR.lodMult),
           overCap: overCap, samples: samples });
    samples = 0;
}

sanity();
setInterval(tick, 16);        // ~60 Hz, to catch the per-frame peak
setInterval(heartbeat, 5000);
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=0, help="stop after N seconds (0 = until Ctrl+C)")
    ap.add_argument("--out", default="probe_drawdistance.log")
    ap.add_argument("--process", default=PROCESS)
    args = ap.parse_args()

    out = open(args.out, "a", encoding="utf-8", buffering=1)

    def emit(line):
        stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        text = f"[{stamp}] {line}"
        print(text, flush=True)
        out.write(text + "\n")

    try:
        session = frida.attach(args.process)
    except frida.ProcessNotFoundError:
        sys.exit(f"{args.process} is not running -- launch the game and get into gameplay first")
    except frida.PermissionDeniedError:
        sys.exit("permission denied attaching -- run this shell as Administrator")
    except frida.ProcessNotRespondingError:
        sys.exit(
            "the process refused to load the frida agent.\n"
            "Almost always elevation: close this shell, open a terminal as "
            "Administrator, and run the same command again. The game does not "
            "need restarting -- a failed injection leaves it running.\n"
            "If it still fails while elevated, that points at the SecuROM layer "
            "rather than permissions, and we fall back to the ASI's own counters."
        )

    emit(f"attached to {args.process} (read-only)")

    def on_message(msg, _data):
        if msg.get("type") == "error":
            emit(f"AGENT ERROR {msg.get('description')}")
            return
        p = msg.get("payload") or {}
        k = p.get("kind")
        if k == "sanity":
            emit(f"image check : {p['state']}")
            emit(f"  LodMultiplier global = {p['lodMult']}")
            emit(f"  0x00BD1008 as int    = {p['areaInt']}")
            emit(f"  0x00BD1008 as float  = {p['areaFloat']}")
        elif k == "peak":
            emit(f"PEAK  visible-object list {p['peak']}/{p['cap']} ({p['pct']}% full)  area={p['areaInt']}")
        elif k == "area":
            emit(f"AREA  {p['from']} -> {p['to']}  (float {p['areaFloat']})  "
                 f"list={p['listCount']} peak={p['peak']} lod={p['lodMult']}")
        elif k == "beat":
            extra = f"  OVER-CAP SAMPLES={p['overCap']}" if p.get("overCap") else ""
            emit(f"beat  list={p['listCount']} peak={p['peak']}/{p['cap']}  "
                 f"area={p['areaInt']} (float {p['areaFloat']})  lod={p['lodMult']}  "
                 f"samples/5s={p['samples']}{extra}")

    script = session.create_script(AGENT)
    script.on("message", on_message)
    script.load()

    try:
        if args.seconds:
            time.sleep(args.seconds)
        else:
            while True:
                time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        emit("detaching")
        try:
            script.unload()
            session.detach()
        except Exception:
            pass
        out.close()


if __name__ == "__main__":
    main()
