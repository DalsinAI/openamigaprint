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
- IPP printer discovery with DNS-SD/mDNS;
- Get-Printer-Attributes capability negotiation;
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
## First-light Amiga installation

Copy:

```
C/OpenAmigaPrint          -> C:OpenAmigaPrint
C/OAPPrintTest            -> C:OAPPrintTest
C/OAPSpoolTest            -> C:OAPSpoolTest
Devs/oapspool.device      -> DEVS:oapspool.device
Devs/Printers/OpenAmigaPrint -> DEVS:Printers/OpenAmigaPrint
```

In **Printer Preferences** select:

- Printer Type: `OpenAmigaPrint`
- Printer Port: device
- Device Unit: `oapspool.device`, unit 0

Then `OAPPrintTest` exercises the ordinary `printer.device` path.

The generated PDF is spooled under `T:OpenAmigaPrint-job-XXXX.pdf`; once `%%EOF` is received, `oapspool.device` launches `C:OpenAmigaPrint` asynchronously.
## Network model

The current first-light client accepts a manual URI such as:

```
ipp://192.168.1.50:631/ipp/print
```

Networking is exclusively through the public `bsdsocket.library` API. Roadshow, AmiTCP, Miami, Genesis, ACNet or another compatible provider can supply that API.

Plain IPP is first light. IPPS will be added behind a TLS abstraction rather than making one network stack mandatory.

## Licence

OpenAmigaPrint source is intended to be freely distributable under the BSD 2-Clause licence. The printer driver is written from scratch against the published classic Amiga printer-driver ABI; AROS sources were used as behavioural reference, not copied into this tree.
