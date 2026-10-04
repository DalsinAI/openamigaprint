# OpenAmigaPrint diagnostic probes

These are investigation-only probes from the First Light printer-device bring-up. They are deliberately outside the normal build and acceptance path.

- `oappreflight.c` includes an early `timer.device` open. During the 3 Oct 2026 Cradle 0.30 update test this stalled when injected into `User-Startup`, before the printer-specific checks could run.
- `oapcallbackprobe.c` directly invokes printer-driver callbacks. The instrumented startup stopped while attempting this probe, so it is not used as an acceptance test.

The supported field path is `tests/oapfieldtest.c`, followed by `OAPPrintTest`. On Instance-23 the field test opened `printer.device`, returned successfully from `PRD_QUERY`, and the print test completed with 214 writes observed by `oapspool.device` and `C:OpenAmigaPrint` launched for the completed PDF job.
