#pragma once

// Source coverage (build.cmd coverage; docs/notes/harness.md, "Source coverage"; tools/coverage.py): the coverage
// build compiles src/ with MSVC's SanitizerCoverage (a flag byte per instrumented edge) and /Gh (every function's entry
// calls _penter, harness_coverage_entry.asm, which marks the function). Write puts what the run has reached so far into
// the run folder's coverage.dat; WriteReport calls it, and the coverage build calls it once more when the process exits.
// In the other builds it does nothing.
namespace ei::verify::coverage {

void Write();

} // namespace ei::verify::coverage
