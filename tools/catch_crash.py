#!/usr/bin/env python3
"""Catch the access violation that Bully throws with LodSwitchScale above 1.0.

Attach this to a running, in-gameplay Bully.exe, then play until it crashes. It
installs an exception handler, writes the faulting address, the faulting memory
access and the full register state to a log, and then lets the crash proceed
normally.

It does NOT patch code and does NOT write to the process. The only thing it adds
is an exception handler, which runs after the fault has already happened.

Why this exists: flt_C3CD00 at 0x00C3CD00 is dormant in the retail game -- ten
reads, no writes, initial value 0.0 -- so sub_5273E0 always returns null and the
full-detail mesh path never runs. Setting it non-zero switches that path on,
which is where the draw-distance quality comes from AND where the crash comes
from. The suspected fault is in sub_452000:

    452230: call eax             ; vtable+0x28, on-demand resource load
    452232: mov  eax, [edi+18h]  ; used with no null check after that load

A faulting address at or just past 0x00452232 confirms it. Anything else means
the guess is wrong again and the real site is wherever this reports.

Usage:
    1. Set LodSwitchScale = 2.0 in Bully-DE.ini
    2. Launch, load a save, get into gameplay
    3. In an ADMINISTRATOR terminal:
         python tools/catch_crash.py --out E:\\BullyDE\\crash.log
    4. Play until it crashes, then send the log
"""

import argparse
import datetime
import sys

try:
    import frida
except ImportError:
    sys.exit("frida is not installed -- run: pip install frida-tools")

AGENT = r"""
const BASE = ptr(0x400000);

function near(addr) {
    // Report the address as a plain VA plus a module-relative note, so it can be
    // looked up directly in the disassembly without extra arithmetic.
    try {
        const m = Process.findModuleByAddress(addr);
        if (m !== null) {
            return addr + "  (" + m.name + " + 0x" + addr.sub(m.base).toString(16) + ")";
        }
    } catch (e) {}
    return addr + "  (no module -- possibly a bad jump or corrupted pointer)";
}

Process.setExceptionHandler(function (details) {
    // Only real faults inside the game image. Without this filter the handler
    // also catches every C++ throw the game makes as ordinary control flow --
    // which is thousands per minute via RaiseException in KERNELBASE, and
    // buries the one event that matters.
    if (details.type !== 'access-violation') return false;
    const inImage = details.address.compare(BASE) > 0 &&
                    details.address.compare(ptr(0x00900000)) < 0;
    if (!inImage) return false;

    const c = details.context;
    const out = {
        kind: "exception",
        type: details.type,
        at: near(details.address),
        memory: details.memory
            ? (details.memory.operation + " " + details.memory.address)
            : null,
        regs: {
            eax: "" + c.eax, ebx: "" + c.ebx, ecx: "" + c.ecx, edx: "" + c.edx,
            esi: "" + c.esi, edi: "" + c.edi, ebp: "" + c.ebp, esp: "" + c.esp,
            eip: "" + c.eip,
        },
    };

    // A few bytes at the faulting instruction help identify it immediately.
    try { out.bytes = details.address.readByteArray(16); } catch (e) { out.bytes = null; }

    // Walk a short stack of plausible return addresses inside the image.
    const stack = [];
    try {
        for (let i = 0; i < 64 && stack.length < 12; i++) {
            const v = c.esp.add(i * 4).readPointer();
            if (v.compare(BASE) > 0 && v.compare(ptr(0x00900000)) < 0) {
                stack.push("" + v);
            }
        }
    } catch (e) {}
    out.stack = stack;

    send(out, out.bytes);
    return false;   // let the crash continue; we only wanted to observe it
});

send({ kind: "ready", lod: ptr(0x00C3CD00).readFloat(), area: ptr(0x00BD1008).readU32() });
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="crash.log")
    ap.add_argument("--process", default="Bully.exe")
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
        sys.exit(f"{args.process} is not running -- get into gameplay first")
    except frida.ProcessNotRespondingError:
        sys.exit("agent refused to load -- run this terminal as Administrator")

    emit(f"attached to {args.process}, waiting for a crash")

    def on_message(msg, data):
        if msg.get("type") == "error":
            emit(f"AGENT ERROR {msg.get('description')}")
            return
        p = msg.get("payload") or {}
        if p.get("kind") == "ready":
            emit(f"handler installed. flt_C3CD00 = {p['lod']}, area = {p['area']}")
            if p["lod"] == 0.0:
                emit("  NOTE: flt_C3CD00 is 0.0, so the crashing path is OFF. "
                     "Set LodSwitchScale = 2.0 in the INI and restart the game.")
            return
        if p.get("kind") == "exception":
            emit("=" * 68)
            emit(f"CRASH  {p['type']}")
            emit(f"  at     {p['at']}")
            emit(f"  access {p['memory']}")
            r = p["regs"]
            emit(f"  eax={r['eax']} ebx={r['ebx']} ecx={r['ecx']} edx={r['edx']}")
            emit(f"  esi={r['esi']} edi={r['edi']} ebp={r['ebp']} esp={r['esp']}")
            if data:
                emit(f"  bytes  {data.hex(' ')}")
            if p.get("stack"):
                emit("  candidate return addresses on the stack:")
                for a in p["stack"]:
                    emit(f"    {a}")
            emit("=" * 68)

    script = session.create_script(AGENT)
    script.on("message", on_message)
    script.load()

    emit("play until it crashes; Ctrl+C here when you are done")
    try:
        import time
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    except frida.InvalidOperationError:
        emit("process gone (this is expected once it crashes)")
    finally:
        out.close()


if __name__ == "__main__":
    main()
