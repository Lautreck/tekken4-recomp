# camera-live notes (in progress)

Chain (live, not p+0xF08): p=u32[0x2AA0D0|0x2AA0D4], cam=u32[p+0xF08], dir=u32[cam+0x198] stride 0x90, target=p+0x660.
VU packet source: slot=0x01CE2270+u32[cam+0x174]*0xC0, copied at FUN_00244fc8 0x2454c4. Only FUN_00244e88 (identity init) and FUN_00244fc8 form that address. FUN_00244fc8 also reads cam+0xE0 (0x40) and cam+0x120 (0x40), scales xyz by 0x3C000000 (0.0078125) at 0x245088, multiplies by the slot, and puts both into the VIF packet. Packet buffer is the return of FUN_00232a60 (0x180 bytes), not a fixed address.
Look-at FUN_00193800 writes only Y of 0x2D27D0/E0/F0. a2/a3 of that call are unread.
Eye FUN_0018e1e0: bnel at 0x18e3c4 is taken (skip to 0x18fca8, delay s3+=0x60) when v0=1 after the matrix multiply into dir+0x40; the other entry (0x18e31c, v0=0) falls into 0x18e3cc. sqrt.s at 0x18e2d4 is sqrt.s f1,f0 (word 0x46010044, fd=1 fs=0) and the C++ matches. Not a decode bug.
Dumps: [camlive] in the override (eye/look/upload) and logs/grok/camera_live_snap.py (PCSX2). Fields: dir90, xform80, target, cam words, cam16, camE0 (32 floats from +0xE0), slot 4x4, basis 0x2D27D0, p0/p1.
Build: out/grok-camera-live, log logs/grok/camera-live-build.log. Probe DumpAt default 62,68,74,80.
