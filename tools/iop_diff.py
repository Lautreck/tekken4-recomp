"""Diff zsiopdrv's data in two IOP RAM images: ours (TEKKEN4_IOP_DUMP=<dir> -> iop_<s>s.bin) against PCSX2's
(a savestate .p2s from tools\\pcsx2_capture.ps1 "<s>:State<n>", or an iopMemory.bin taken out of one).

PCSX2's real IOP loads more modules, so zsiopdrv sits elsewhere: the load delta comes from code windows of ours
found in PCSX2's RAM. A word counts as equal when it matches directly or is a pointer into the module that
differs by exactly that delta. Heap pointers (the 0xc7000 ring, the 0xc8000/0x108000 disc buffers) are printed
as they are, both values side by side.

  python -I tools/iop_diff.py logs/iopdump1/iop_40s.bin external/pcsx2/sstates/<file>.p2s [--lo 0x51a00 --hi 0x53400]
"""
import argparse
import collections
import struct
import sys
import zipfile

RAM = 2 * 1024 * 1024
# Code windows of ours without lui/jal (they relocate): 0x4e6d4 command list loop, 0x4a878 stream object init.
NEEDLES = [0x4E6D4, 0x4A880, 0x4AA00, 0x4EC1C, 0x4D5B4]


def load(path):
    if path.lower().endswith(".p2s"):
        with zipfile.ZipFile(path) as z:
            names = [i for i in z.infolist() if "iop" in i.filename.lower() and i.file_size == RAM]
            if not names:
                sys.exit("no 2 MB iop entry in %s: %s" % (path, [i.filename for i in z.infolist()]))
            return z.read(names[0])
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < RAM:
        sys.exit("%s: %d bytes, want %d" % (path, len(data), RAM))
    return data[:RAM]


def find_delta(ours, theirs, args):
    votes = collections.Counter()
    for a in NEEDLES:
        needle = ours[a:a + 16]
        pos = theirs.find(needle)
        while pos >= 0:
            votes[pos - a] += 1
            pos = theirs.find(needle, pos + 4)
    if not votes:
        sys.exit("zsiopdrv code not found in the PCSX2 image")
    # PCSX2 also keeps the raw IRX file the EE sent (LoadModuleBuffer) with unrelocated pointers: take the copy
    # whose module pointers moved by the delta.
    def relocated(delta):
        n = 0
        for a in range(args.lo, args.hi, 4):
            wo = struct.unpack_from("<I", ours, a)[0]
            if args.mod_lo <= wo < args.mod_hi and 0 <= a + delta and a + delta + 4 <= RAM:
                n += struct.unpack_from("<I", theirs, a + delta)[0] == wo + delta
        return n
    delta = max(votes, key=lambda d: (relocated(d), votes[d]))
    return delta, votes[delta]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ours")
    ap.add_argument("theirs")
    ap.add_argument("--lo", type=lambda s: int(s, 0), default=0x51A00)
    ap.add_argument("--hi", type=lambda s: int(s, 0), default=0x53400)
    ap.add_argument("--mod-lo", type=lambda s: int(s, 0), default=0x48000)
    ap.add_argument("--mod-hi", type=lambda s: int(s, 0), default=0x54000)
    ap.add_argument("--max", type=int, default=400)
    args = ap.parse_args()
    ours, theirs = load(args.ours), load(args.theirs)
    delta, n = find_delta(ours, theirs, args)
    print("delta %+#x (%d of %d code windows agree): our 0x%x = PCSX2 0x%x" % (delta, n, len(NEEDLES), args.lo, args.lo + delta))
    same = diff = 0
    for a in range(args.lo, args.hi, 4):
        b = a + delta
        if b < 0 or b + 4 > RAM:
            continue
        wo = struct.unpack_from("<I", ours, a)[0]
        wp = struct.unpack_from("<I", theirs, b)[0]
        if wo == wp or (args.mod_lo <= wo < args.mod_hi and wp == wo + delta):
            same += 1
            continue
        diff += 1
        if diff <= args.max:
            note = ""
            if args.mod_lo + delta <= wp < args.mod_hi + delta:
                note = "  (PCSX2 = module ptr, ours-equiv 0x%x)" % (wp - delta)
            print("0x%05x  ours %08x  pcsx2 %08x%s" % (a, wo, wp, note))
    print("%d words equal, %d differ" % (same, diff))


if __name__ == "__main__":
    main()
