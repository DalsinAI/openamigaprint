# OpenPrint First Light - How To Test

This procedure validates the classic AmigaOS `printer.device` to PDF path used by OpenPrint First Light.

## 1. Virtual printer status

Open a Shell and run:

    OAPStatusTest

Expected result:

    PASS: virtual PDF printer is ready and can never report paper out

The status byte must not contain the parallel `PAPEROUT` or `PARBUSY` bits. OpenPrint is a virtual PDF spooler; physical paper and printer-busy conditions do not apply. Spool creation or write failures are I/O failures, not paper faults.

## 2. printer.device path

Run:

    OAPPrintTest

Expected Shell result:

    OpenPrint: text job submitted

The job should pass through `printer.device`, `DEVS:Printers/OpenPrint`, and `oapspool.device`, create a PDF under `T:`, and open the native OpenPrint window.

No `Printer Trouble: Out of paper` requester is valid during this test.

## 3. Save and inspect the PDF

Choose **Save PDF** in the OpenPrint window. Open the saved file in a PDF reader or an installed PDF datatype.

The page should contain the OpenPrint First Light test text. First Light's built-in preview remains a placeholder and is not yet a general PDF renderer.

## First Light pass criteria

- `OAPStatusTest` reports ready with no paper-out/busy state.
- `OAPPrintTest` completes without a physical-printer trouble requester.
- A valid PDF is produced and can be saved.
- The PDF contains the expected test text.

Printer discovery, IPPS/TLS, capability negotiation, general PDF preview rendering and broad application qualification are separate later gates.
