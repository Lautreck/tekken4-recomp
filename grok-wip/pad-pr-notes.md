Report an open pad port with no controller as disconnected

Problem

scePadGetState returned stable (6) for every port the game had opened. scePadPortOpen still succeeds when nothing is plugged in. On a real PS2, scePadGetState then stays at scePadStateDiscon (0) and scePadRead returns no packet. Tekken opens both ports, so with one controller port 2 looked plugged in: PRESS P2 START, and VS BATTLE was enabled.

The host path did not distinguish ports. The shared pad backend ignores port and slot. The stub fallback sampled the first host gamepad plus the keyboard for every port.

Per-file changes

ps2xRuntime/src/lib/Kernel/Stubs/Pad.cpp
Port 0 stays the keyboard plus the first host gamepad. Its read path is unchanged: the backend, or else the first gamepad plus the keyboard. Each later port is the next host gamepad (findNthGamepad). That port is read from that gamepad directly, because the backend cannot address it. scePadPortOpen still succeeds when nothing is plugged in. An open port with no controller: scePadGetState returns 0. scePadRead returns 0 and fills 32 bytes with 0xFF (idle SIO line, no button held). scePadInfoMode, scePadInfoAct, and scePadInfoPressMode return 0. scePadGetReqState returns 0 (complete, not busy). Mode, actuator, pressure, and vref requests return 0 and do not queue EXECCMD.
setPadTestHostGamepadCount is a test hook (not in Pad.h). -1 keeps the raylib query. 0..4 pretends that many host gamepads exist, in index order. Raylib has no virtual gamepad, and the test suite has no window, so this is how the suite forces port 1 empty or connected. findFirstGamepad ignores it, so port 0 is unchanged.

ps2xTest/src/pad_input_tests.cpp
"empty open port 1 reads disconnected while port 0 stays stable": one pretended host gamepad. PortOpen on port 1 still returns 1. scePadGetState is 0. scePadRead returns 0 and the 32-byte buffer is 0xFF. InfoMode, InfoAct, InfoPressMode, and GetReqState return 0. SetMainMode returns 0 and the port stays disconnected. Port 0 in the same run stays STABLE (6), scePadRead returns 1, and the packet is digital (0x41), not an idle 0xFF line.
"port 1 with a second host gamepad reads like a connected pad": two pretended host gamepads. Port 1 is STABLE. scePadRead returns 1. The override buttons and axes match port 0. Without an override the packet is a normal digital idle pad (mode 0x41, buttons released), not 0xFF. InfoMode CURID is 4, InfoPressMode is 1, InfoAct reports 2 actuators. SetMainMode returns 1, then scePadGetState is EXECCMD once and STABLE after that.

Found while

Tekken 4 recomp, PRESS P2 START / VS BATTLE with one pad. On the patched tree the same Pad.cpp change greyed VS BATTLE and the cursor skipped it, and character select showed CREDIT 0 (logs/grok/pad2-fix-notes.md). No second physical gamepad was plugged in for that capture, or for this test run.

Verified with

cmd /c external\ps2recomp-pr8\build_pr_tests.cmd on branch pad-empty-port-disconnected (eb8688b). 447 tests, 447 passed, 0 failed.
empty open port 1 reads disconnected while port 0 stays stable.
port 1 with a second host gamepad reads like a connected pad.
Existing PadInput cases (port 0 init, STABLE, EXECCMD, override read, pressure mask) still pass.

Made by Pablo and assisted with Claude Code and Grok.
