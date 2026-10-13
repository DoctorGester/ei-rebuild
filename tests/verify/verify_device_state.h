#pragma once

// verify_device_state.cpp (A-S13): the device object's state after start-up, written to ei_hook.log
// once ("device-state: ..." lines, nothing in them differs between runs on one machine). The ports
// of the device's constructors cannot run next to the originals (one DirectDraw primary surface, one
// Direct3D device), so the state they leave is compared between two runs instead: one with ours, one
// with their addresses in disabled.txt (the originals run). Called from the first texture bind or
// batch flush, which both kinds of run reach.

class CTextureDevice;

void DumpDeviceStateOnce(const CTextureDevice* device);
