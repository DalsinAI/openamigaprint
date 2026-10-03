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
    +-- IPP/IPPS over bsdsocket.library
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
- optional `ipps://` transport through AmiSSL v5 with peer and hostname verification;
- copies, A4/Letter, portrait/landscape, colour/mono, duplex and page-range IPP attributes;
- host IPP/PDF codec tests;
- Amiga spool and printer.device smoke-test programs.

The code is source-built independently of AmigaChrome.
## Not yet implemented

The following are intentionally not claimed by first light:

- real PDF page rendering inside the preview pane;
- IPP printer discovery with DNS-SD/mDNS;
- Get-Printer-Attributes capability negotiation;
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

Classic AmigaOS 3.x, plain IPP:

```sh
./build-amiga.sh
```

Enable IPPS by pointing the build at an AmiSSL v5 Developer directory:

```sh
AMISSL_SDK=/path/to/AmiSSL/Developer ./build-amiga.sh
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

The client accepts manual printer URIs such as:

```
ipp://192.168.1.50:631/ipp/print
ipps://printer.example.net:631/ipp/print
```

Networking is exclusively through the public `bsdsocket.library` API. Roadshow, AmiTCP, Miami, Genesis, ACNet or another compatible provider can supply that API.

IPPS is optional. AmiSSL v5 is loaded only for `ipps://` jobs, with SNI, trust-store validation and hostname verification. Plain IPP continues to work without AmiSSL.

The IPPS first-light gate was exercised on AmigaOS 3.2.3 using ACNet's `bsdsocket.library` and AmiSSL 5.27: a trusted endpoint completed TLS and reached HTTP, while a deliberate hostname mismatch was rejected with X509 verify result 62.

## Licence

OpenAmigaPrint source is intended to be freely distributable under the BSD 2-Clause licence. The printer driver is written from scratch against the published classic Amiga printer-driver ABI; AROS sources were used as behavioural reference, not copied into this tree.
