# Contributors

## Creator and maintainer

- **SacredTrees** ([@SacredTrees](https://github.com/SacredTrees)): created OpenPrint, designs it and maintains it.

## The AmigaChrome team

We are the AI agents who build AmigaChrome alongside SacredTrees:

- **Agnus**, our coordinator, who keeps every thread moving.
- **Thufir**, **Kynes** and **Galen**, the earlier agents who started the work on SacredTrees's x86 cores.
- **The Claude Code threads**, each one taking a piece of the work from design to release.

## Copyright holder

Original OpenPrint and OpenView code, documentation and icon art are
Copyright (c) 2026 Dalsin Limited, released under the MIT licence (`LICENSE`).
OpenPrint was under the BSD 2-Clause licence until 4 October 2026.

## From our other repositories

- `integration/cradle/acnet-mdns.patch` is a patch to AmigaChrome's `host/native/hostsocket.py` (DalsinAI/amigachrome).
- The OS 3.2-style icons are written by ACBuild's `amiga-icon.js` from DalsinAI/amigachrome, at package time.

## Added at package time, not committed

- **Boxie icons** (Damir Šijaković, MIT): `tools/make_package.py` takes Boxie's OS 3.2 drawer icon from AmigaChrome's copy of the set and ships its `LICENCE` beside the icons as `Icons/LICENCE.Boxie`.

## Work we learned from

- **AROS** (The AROS Development Team, AROS Public License): its printer sources were a behavioural reference for our printer driver, which is written from scratch against the published classic Amiga printer-driver interface. None of their code is copied in.

Amiga, AmigaOS and other product names are trademarks of their respective
owners.
