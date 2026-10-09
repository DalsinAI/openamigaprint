# PDF printer browser - native GadTools delivery

## What this change installs

`C:OAPPrinters` is the Printers and Queue window, in GadTools (it was ReAction until 4 October 2026). `C:OAPDiscover` is its separate network worker. Both the existing OpenPrint per-job dialog and OpenView have a Browse button. The existing printer driver and spooler do not need replacement for this change.

The browser discovers `_ipp._tcp.local` and `_ipps._tcp.local`, resolves each service's own SRV target and A record, uses its advertised port and TXT `rp` path, and sends IPP Get-Printer-Attributes. It does not assume that a device answering mDNS is a PDF printer.

The default list includes only endpoints with a successful, complete IPP response explicitly containing `application/pdf` in `document-format-supported`. Show all printers exposes unsupported and unverified entries with an explanation. Use Printer remains disabled for those entries. Query address offers the same verification for a manual IPP URI.

Selection is saved in `ENV:OpenPrint/PrinterURI` and `ENVARC:OpenPrint/PrinterURI`. The parent windows notice the saved selection without needing to restart. Print rechecks the capability before uploading, so a saved selection cannot bypass verification.

## Quick test

1. Launch `C:OAPPrinters` on a working Amiga TCP/IP stack providing `bsdsocket.library`.
2. Wait for discovery to complete. Select a row marked Confirmed and choose Use Printer.
3. Launch `C:OpenPrint Work:OpenPrint.pdf` for the known small test page. The selected destination should already be filled in. Existing open dialogs refresh their saved destination automatically.
4. Set one copy and one-sided, then click Print once.
5. An accepted IPP job-id is evidence of submission, not evidence of physical printing. Check the printer output before reporting an end-to-end pass. Do not repeatedly resend an uncertain submission.

For image printing, open the image in OpenView (or drop it on its window), set Paper, Turn and Size under Page setup, then choose Print... . The viewer makes the PDF and opens the same Print requester on it. This window does not add missing document codecs or replace the viewer's rendering engine.

## Build and tests

Run `make test test-discovery`, `./build-amiga.sh`, `./build-viewer.sh`, and `./build-browser.sh`.

Install the matching `OpenPrint`, `OpenView`, `OAVWorker`, `OAPDiscover`, and `OAPPrinters` binaries from `build/amigaos3` into C:. The optional Tools: copies provide Workbench entry points. Give each executable Amiga read and execute permission. Amiga protection bits are inverted for RWED: a set execute-inhibit bit prevents loading even when Linux's executable permission is set.

The regression suite covers exact MIME matching, missing capability attributes, mismatched request identifiers, rejected responses, truncated IPP bodies, split HTTP responses, 100 Continue, chunked framing, conflicting lengths, DNS compression bounds, truncated DNS packets, unordered service/address records, advertised non-default ports, and successful job-id parsing.

## Known limits

- IPPS endpoints are checked and printed to through the TLS abstraction (README, "IPPS"): opentls.library or AmiSSL 5. A printer offering both is listed once, by IPPS when a TLS library is there. A printer's own certificate is trusted by asking, once, and remembered by fingerprint; no silent TLS downgrade occurs, and an IPPS printer is never quietly swapped for its plain IPP twin. (The earlier AmiSSL-only `feature/ipps-firstlight` branch is superseded.)
- IPv4 local-network mDNS only. No IPv6, routed discovery directory, SSDP fallback, or persistent discovery daemon is claimed.
- Advertisements with missing target/address/port/resource path remain unverified rather than guessing an endpoint.
- MintPRINT's Discover / Query separation and endpoint handling were reviewed as a reference. No MintPRINT implementation or artwork has been copied into this change.
- Physical job completion and cancellation at the remote printer are not monitored yet.
- Instance-23 native network validation is currently blocked while opening its installed `acnet.device`. Host-side discovery has been exercised successfully; this is not a claim that native discovery or the native UI has passed its live acceptance test.

## Protocol references

DNS-SD records: RFC 6763. One-shot mDNS clients: RFC 6762 section 6.7. IPP Get-Printer-Attributes and document-format-supported: RFC 8011. Implementation and tests in this change are original code, under OpenPrint's MIT licence.

## 3 October follow-up: empty Show all list

The host-side ACNet policy was found to reject the mDNS multicast destination.
The narrow runtime fix and repeatable tests are in `integration/cradle/` and
AmigaChrome commit e9e046f. The worker now checks every discovery send and exposes
errors instead of reporting a successful zero-result scan. Completed scans report
received/parsed packet counts. Opening the browser no longer prints a debug line
that can cause an unnecessary Workbench output console.

The build scripts now use strong library-base definitions (`-fno-common`) so the
runtime does not pull in libnix's unintended window.library auto-opener. This
preserves the explicit library lifecycle.

Native discovery now has an observed pass: the ReAction default view showed
the colour HP as PDF confirmed/ready, and Show all subsequently showed six
IPP/IPPS endpoints across the two HPs and Epson. Network startup was intermittent
during testing; see integration/cradle/README.md for the failure and recovery
sequence. Physical job completion is still a separate, unverified gate.

## Print-window crash recovery

See `PRINT_RECOVERY_AND_DOCUMENTS.md`. Print submission now uses `C:OAVWorker`; install it with the matching application build. Recovery, request-option preservation and host transport tests do not constitute a physical-print pass.

## Explicit selection events

Use Printer now verifies the saved ENV:/ENVARC: value and notifies every open
print/viewer subscriber, including when the same printer is selected again.
It no longer relies solely on comparing saved URI strings on window ticks.
See PRINTER_SELECTION_HANDOFF.md for the reproduced failure and native test.
