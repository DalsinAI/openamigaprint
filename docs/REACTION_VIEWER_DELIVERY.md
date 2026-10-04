# OpenAmigaView / OpenAmigaPrint delivery contract

Dale's accepted scope, 3 October 2026: deliver all five application phases,
with a native ReAction UI, a datatype-based universal viewer, good rendering,
image-to-PDF/printing, a shared durable queue and ARexx automation. The working
name of the viewer is OpenAmigaView. This is not a browser application and must
remain usable without AmigaChrome or a Linux conversion service.

## Non-negotiable behaviour

- ReAction, not the earlier GadTools shell. Use installed OS 3.2 classes first.
- Open/view/play and print/export are separate capabilities. Opening the first
  frame of an animated GIF does not qualify as animated GIF playback.
- PDF is an output/provider, not the universal internal representation. Source
  identity, page geometry, selection and rendering settings are explicit.
- Preview scaling must not reduce the source resolution used for export.
- Physical printer errors belong to delivery, not virtual PDF capture.
- A submitted IPP job is not yet a confirmed physically printed job.
- Decode/render work and physical delivery must not run in Intuition hooks.
- No document macros, embedded programs, external resource fetching or ARexx
  supplied by the document are executed. ARexx is a user-controlled host API.
- Report unavailable providers explicitly. Never disguise extraction as layout.
- No third-party binaries are bundled without redistribution rights.

## Five delivery phases (all authorised; no further spec approval gate)

1. ReAction workspace: Open, source view, page layout, fit/zoom, playback,
   status, queue, preferences, Workbench entry and dependency diagnostics.
2. Datatype picture pipeline: full-resolution source, alpha/palette handling,
   aspect-correct composition, A4/Letter and orientation, PDF export and queue.
3. Shared queue: classic printer.device capture and viewer jobs together;
   durable source/settings, errors, cancellation, submission and history.
4. Providers/rendering: native image/text/animation first; true animated GIF,
   WebP, paged PDF, DOCX, PPTX and selected video containers/codecs; custom
   BOOPSI viewport/timeline classes only when standard classes are insufficient.
5. ARexx: same command dispatch as GUI, reliable RC/RESULT, job identifiers,
   no blocking render in callbacks, sample scripts and automated regressions.

## Format qualification matrix

Every row needs read, display, navigation/playback, export and fixture results.
An installed datatype is a dependency, not an automatic test pass.

| Family | Native integration route | Remaining provider work |
| --- | --- | --- |
| IFF/ILBM, ACBM, JPEG, PNG, BMP, GIF still | picture datatype | alpha, scaling and export fixtures |
| WebP still/animated | picture/animation provider | audit open-source libwebp and native build |
| IFF ANIM, CDXL | animation/movie datatype | playback, seeking and frame-print fixtures |
| Animated GIF | animation provider | gif disposal, transparency, timing, looping; no first-frame shortcut |
| TXT/AmigaGuide | text/document datatype | safe links, searching, print pagination |
| PDF | installed PDF datatype initially | independent renderer decision and licence audit; no rebundling existing personal-use datatype |
| DOCX | ZIP/OOXML document provider | paragraphs, runs, tables, pictures, pagination, fonts, bounded packages |
| PPTX | ZIP/OOXML slide provider | slide masters, shapes, text, images and transforms; notes separate |
| Video | container + codec + timed AV provider | enumerate tested pairs; do not claim all MP4/AVI files work |

## Architecture

OpenAmigaView ReAction UI and ARexx -> shared commands -> immutable job request
-> native worker -> provider -> page composition -> output backend -> OAP queue.
DataType objects are hosted with AddDTObject/RemoveDTObject, not handed to
layout.gadget as ordinary owned children. Input data and the printer service
are not rewritten just to make a screenshot or demonstration work.

Preview uses a distinct DataType instance. Worker decoding uses original source
pixels. New formats use the same provider capability contract rather than
hard-coded filename-to-launch-command shortcuts.

## Acceptance gates

A real Workbench launch, resizable render, image selection, correct-aspect PDF,
queue entry, successful ARexx invocation, and no stale startup hooks. Preserve
all First Light printer regressions. Record NOT TESTED rather than inventing
renderer or physical-printer success. Keep implementation checkpoints in Git.

References: https://developer.amigaos3.net/autodocs/datatypes.library/
https://developer.amigaos3.net/autodocs/animation.datatype/
https://wiki.amigaos.net/wiki/Writing_Datatype_Classes
