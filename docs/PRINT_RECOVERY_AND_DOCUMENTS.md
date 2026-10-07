# Print recovery and document-to-PDF routes

## Incident: 3 October 2026

Instance-23 stopped updating at "Sending job to printer". The native runtime
was repeatedly faulting (over 650,000 access faults in a five-second sample).
The old transport routine's compiler-reported stack requirement was 14,084
bytes. The installed OpenPrint Workbench icon requested only 8,192 bytes,
and the application did not declare a larger libnix stack. This is a concrete
stack-overflow defect; the exception log alone does not prove every fault's
origin. The print dialog also performed networking synchronously.

The repair moves the transport workspace to heap storage: the same compiler
now reports 96 bytes for the extended transport routine. OpenPrint and
its generated icon request 65,536 bytes. The print dialog submits a versioned
request to the separate OAVWorker process instead of uploading in its event
loop. Copies, paper, orientation, colour, duplex and page ranges travel with
the request. Existing schema-2 viewer requests remain readable.

Workers publish preparing/checking/connecting/uploading/awaiting-reply states.
The UI stays in its input loop, polls progress, and requests cooperative stop
via the job's cancel marker. A cancellation after transmission starts, a lost
reply, and a missing successful job-id are reported as uncertain. No automatic
retry occurs. "Submitted" is not "Printed". Stopping an upload is not the same
as cancelling an already accepted remote printer job.

The previous 43,976,548-byte job is retained, marked uncertain after recovery.
The HP rejected all/completed/not-completed Get-Jobs probes, so those probes
cannot establish whether the earlier submission printed. Recovery does not
resend it. The driver, spooler, startup scripts and printer settings were not
replaced by this repair.

## Current document support, not a universal-format claim

- Direct picture export: an installed picture datatype supplies original
  resolution pixels to the page/PDF worker. Preview screen pixels are not used
  as the print source. The current implementation is bounded to 16 megapixels
  and snapshots up to 64 MiB. JPEG, PNG, ILBM and other picture formats depend
  on the matching installed datatype. Multi-frame/animated input is not a
  claim of full animation-to-document support.
- Existing PDF: copied unchanged. Layout controls are not reapplied to that
  PDF. A PDF reader is not required merely to forward the existing PDF.
- Native application printing: a program that can open its document and uses
  normal printer.device text or raster printing can render it through the
  OpenPrint driver. Application compatibility still needs testing.
- Text/document datatypes: the viewer may display them, but this worker's
  direct export is currently restricted to GID_PICTURE. Viewing and PDF export
  are different capabilities. Generic DTM_PRINT output is not wired here yet.
- DOCX/PPTX and other rich documents: no native layout renderer is installed
  by this repair. Filename recognition, extracting XML text, or renaming the
  extension is not faithful document rendering.

## The shared conversion contract

The next conversion work should use:

    Open -> detect format -> select available decoder/renderer
         -> paginated document/page model -> preview or PDF writer
         -> durable queue -> explicitly selected destination

Each backend must report View, Paginate, Export PDF and Print capabilities
separately. The viewer enables only the supported actions and explains a missing
backend. Work occurs in a worker, with progress, cancellation and preserved
input/output. Office backends must lay out text, fonts, images, tables and
slides; animation/video export must explicitly select a frame or contact sheet.
Native datatypes with DTM_PRINT can be an additional compatibility route, but
must use a correlated output job rather than guessing the latest spool file.
The standalone native core must not silently depend on a host/cloud converter.
An optional host renderer would need to be separately installed and clearly
identified to the user.

## Use on Instance-23

For a supported image: open OpenView, Open the picture (or drop it on the
window), set Paper, Turn and Size under Page setup, then Print... . The viewer
renders the page to a queued PDF (original-resolution source retained) and opens
the Print requester on it; Save as PDF... writes it to a file instead. A PDF
opened in the viewer goes straight to the Print requester. Printers and
queue... opens C:OAPPrinters, which still verifies application/pdf. The old large uncertain job must not be retried
until its printer outcome is checked.

## Checks

- make test test-discovery test-submit
- Submission tests use mocked sockets, never a physical printer.
- ASan/UBSan run on the host transport test.
- Compiler stack reports are in docs/history/evidence/print-stack-before.txt and
  print-stack-after.txt.
- OAVRequestSmoke runs on the guest, checks request versions and options, then
  launches a real separate worker with a deliberately invalid destination.
  Work:OAPWorkerSmoke.txt records the result; no print job can be sent by it.
- The large-image physical print and full document-format matrix remain open
  acceptance gates. A successful error-path smoke test does not close them.
