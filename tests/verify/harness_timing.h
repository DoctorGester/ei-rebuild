#pragma once

#include <cstdio>

// Where a harness run's wall time goes (verify build only; harness_timing.cpp, SV10): the main thread's
// time per main loop frame split into the server's world updates, the state digest, the client's ticks,
// the harness's steps, a pair's wait for the other game and the out-of-sight window checks; the rest is
// the interface, the camera, the frame step (drawing and the present) and the message loop. Each part
// counts its wall time and the main thread's CPU cycles. The report's TIMING: lines say the totals, the
// start-up (process start to the first frame), the frames that took long (zone loads) and the process's
// CPU time. Nothing the game reads changes: the counters are the harness's own.
namespace ei::verify::timing {

enum Part { Server, Digest, Client, Steps, PairWait, WindowChecks, Parts };

// The main thread's wall time and cycles spent in a part while the scope lives.
class Scope {
public:
    explicit Scope(Part part);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Part part_;
    int outer_;   // the part this scope interrupted (-1: none), resumed at its end
};

// A frame of the main loop has ended (the after-frame peek, before a pair's turn is handed on).
void FrameEnd();

// The report's TIMING: lines.
void Report(FILE* f);

} // namespace ei::verify::timing
