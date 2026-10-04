# OpenAmigaPrint

OpenAmigaPrint is a standalone, freely distributable modern printing stack for classic Amiga systems.

Its design goal is deliberately conventional on the Amiga side:

```
application
    |
   PRT: / printer.device
    |
DEVS:Printers/OpenAmigaPrint
    |  streaming PDF 1.4
    v
DEVS:oapspool.device
    |  T: spool, document boundary at %%EOF
    v
C:OpenAmigaPrint
    |
    +-- Save PDF
    +-- native Amiga print window
    +-- IPP over bsdsocket.library
```

There is no AmigaChrome dependency in the core stack.
## First-light status

Implemented and building for 68000-class AmigaOS 3.x:

- classic V35 printer-driver segment;
- text-to-PDF streaming through `printer.device`;
- `PRD_DUMPRPORT` RGB raster path;
- bounded-memory PDF generation with xref/trailer;
- `oapspool.device` port device and PDF job boundary detection;
- native GadTools/ASL print window;
- Save PDF;
- plain `ipp://` Print-Job submission through `bsdsocket.library`;
- copies, A4/Letter, portrait/landscape, colour/mono, duplex and page-range IPP attributes;
- host IPP/PDF codec tests;
- Amiga spool and printer.device smoke-test programs.

The code is source-built independently of AmigaChrome.
## Not yet implemented

The following are intentionally not claimed by first light:

- real PDF page rendering inside the preview pane;
- fully qualified native discovery on all target TCP/IP stacks;
- capability negotiation beyond the PDF/accepting-jobs gate;
- IPPS/TLS;
- PWG Raster fallback for printers that do not accept PDF;
- complete mapping of every classic text-style command;
- broad application compatibility testing;
- qualification on real hardware;
- automated installer or Prefs application.

The current preview identifies the real PDF job and displays a page placeholder. It is not yet a PDF interpreter.

## Build

Host core:

```sh
make test
```

Classic AmigaOS 3.x:

```sh
./build-amiga.sh
```
## Installing with the Amiga Installer

Build the programs (`./build-amiga.sh`, `./build-browser.sh`, `./build-viewer.sh`), then the package:

```sh
python3 tools/make_package.py --lha-module ~/AmigaChrome/scripts
```

`build/package/OpenAmigaPrint` is a drawer to copy to the Amiga; `build/package/OpenAmigaPrint.lha` is the same as one archive (`--lha-module` names the folder with AmigaChrome's `lha_archive.py`; without it only the drawer is made). On the Amiga, double-click **Install OpenAmigaPrint** (it runs SYS:System/Installer on OS 3.2, C:Installer elsewhere). It installs the five commands in C:, `oapspool.device` in DEVS:, the printer driver in DEVS:Printers, the spooler in WBStartup if you want it, and an OpenAmigaPrint drawer (SYS:Utilities by default) with OpenAmigaView, Printers and Queue, a test picture and a test page. The script is `package/Install-OpenAmigaPrint`.

## First-light Amiga installation

Copy:

```
C/OpenAmigaPrint          -> C:OpenAmigaPrint
C/OAPPrintTest            -> C:OAPPrintTest
C/OAPSpoolTest            -> C:OAPSpoolTest
C/OAPStatusTest           -> C:OAPStatusTest
Devs/oapspool.device      -> DEVS:oapspool.device
Devs/Printers/OpenAmigaPrint -> DEVS:Printers/OpenAmigaPrint
OpenAmigaPrint.info         -> DEVS:Printers/OpenAmigaPrint.info
```

In **Printer Preferences** select:

- Printer Type: `OpenAmigaPrint`
- Printer Port: device
- Device Unit: `oapspool.device`, unit 0

Run `OAPStatusTest` first to verify the virtual port reports ready with paper-out and busy permanently clear. Then `OAPPrintTest` exercises the ordinary `printer.device` path. See `HOW_TO_TEST_FIRST_LIGHT.md` for the complete acceptance test.

The generated PDF is spooled under `T:OpenAmigaPrint-job-XXXX.pdf`; once `%%EOF` is received, `oapspool.device` launches `C:OpenAmigaPrint` asynchronously.
## Network model

The current first-light client accepts a manual URI such as:

```
ipp://192.168.1.50:631/ipp/print
```

Networking is exclusively through the public `bsdsocket.library` API. Roadshow, AmiTCP, Miami, Genesis, ACNet or another compatible provider can supply that API.

Plain IPP is first light. IPPS will be added behind a TLS abstraction rather than making one network stack mandatory.

## Licence

OpenAmigaPrint is free software under the MIT licence (`LICENSE`, Copyright (c) 2026 Dalsin Limited): anyone may use it, change it, fork it and ship it, commercially too. Dale, 4 October 2026: projects we call Open are MIT. It was BSD 2-Clause until then.

The licence's one condition keeps the credit: the copyright notice and the licence text stay with every copy and fork. We also ask, as a courtesy rather than a condition, that a fork or a port say it is based on OpenAmigaPrint by Dalsin Limited.

The printer driver is written from scratch against the published classic Amiga printer-driver ABI; AROS sources were used as behavioural reference, not copied into this tree.

## Queue and graphics printing

OpenAmigaPrint now treats completed printer jobs as durable queue entries rather than transient one-shot files.

- Install `OAPSpooler` as a Workbench-startup tool (`SYS:WBStartup/OAPSpooler`) with `DONOTWAIT`.
- The spooler stores completed jobs under `SYS:Spool/OpenAmigaPrint/` as a PDF spool artifact plus a `.job` metadata record.
- Install `OpenAmigaPrint` plus `OpenAmigaPrintTool.info` as `SYS:Tools/OpenAmigaPrint` and `SYS:Tools/OpenAmigaPrint.info`. Double-click it, or run `OpenAmigaPrint` with no arguments (or `OpenAmigaPrint QUEUE`), to open the native queue window.
- Selecting **Open** on a queued job opens the native per-job window, where the job can be saved/exported or sent to an IPP printer.
- `OAPImageTest` exercises the real `PRD_DUMPRPORT` graphics path. By default it prints a full-width, aspect-correct, centered colour test card; pass `1TO1` for a diagnostic unscaled dump.

The classic printer-driver graphics path uses the canonical ExecBase pointer at absolute address 4, as required by traditional Amiga printer-driver init glue. Raster transfer honours `pi_xpos` and `pi_ScaleX`, so printer.device controls rotation, centering and scaling according to the active printer preferences.

## PDF printer browser

The windows are GadTools (Dale, 4 October 2026: OS 3.x applications use GadTools or MUI, not ReAction), so they run on AmigaOS 3.0 and later. The `feature/pdf-printer-browser` integration adds `OAPPrinters`, an asynchronous DNS-SD discovery worker, explicit PDF capability checks, and Browse buttons in the print dialog and native viewer. Print-Job submission rechecks PDF support and validates a complete IPP response with a job identifier. See [PRINTER_BROWSER.md](docs/PRINTER_BROWSER.md) for installation, native validation status, and current protocol limits.
