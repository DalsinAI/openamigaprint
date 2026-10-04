# Printer selection hand-off repair

## Report and reproduction

We reported that Use Printer did not update the open print window.
On Instance-23 the print dialog displayed the deliberately invalid destination
left by the earlier safe print-worker smoke test. The persisted default was
already the colour HP. The old polling code compared the saved URI against
its cached saved URI, not against an explicit user selection event. Selecting
the same saved HP could therefore leave the visible field unchanged.

The live test also exposed a save-path failure: the browser wrote ENVARC: but
its delete-and-rename sequence did not successfully finish the ENV: update.
The network-status polling could hide the save error. This is distinct from
the stale saved-URI comparison. No particular ENV-handler internal cause is
claimed from the filesystem trace alone.

## Repair

- Each open OpenPrint dialog and OpenView window registers a named
  Exec message port. The browser publishes a bounded, copied selection event
  only after validating the PDF-capable choice and saving it successfully.
- Every explicit Use Printer is an event, even when its URI is unchanged.
  Delivery wakes the receiver independently of Intuition ticks/window focus.
- The visible destination and the dialog's options update together. The
  immutable request of an already-running print worker is not modified; the
  print dialog defers applying a newly received choice until that worker ends.
- Selection also reaches already-open subscribed windows when the browser was
  launched separately from Workbench. Opening/closing a browser without Use
  does not publish a selection. Persistent URI loading remains a fallback.
- Preferences use SetVar with GVF_GLOBAL_ONLY and GVF_SAVE_VAR, followed by
  GetVar and ENVARC: read-back verification. No delete-and-rename operation is
  performed on the active ENV: value. Save failures remain visible.
- Event messages use AllocVec public memory with an inline URI. Ownership
  transfers at PutMsg; receivers free messages after GetMsg or on shutdown.
  Port names are snapshotted under Forbid; FindPort and PutMsg are arbitrated
  together. No allocation or file operation occurs inside those list locks.
  There is a bounded limit of 64 simultaneously subscribed windows.

## Actual Instance-23 acceptance

1. Installed the repaired OpenPrint, OpenView and OAPPrinters in C:
   and their existing SYS:Tools locations. No driver, worker, startup-script,
   or Cradle runtime replacement and no instance reboot were required.
2. Ran OAPSelectionSmoke in the isolated OAP.TestSelection. namespace:
   PASS, 200 native checks, zero failures. These include repeat selection,
   multiple subscribers, last-event-wins, malformed destinations, queued
   shutdown, unrelated-message ownership, an independent sending process,
   and read-back of a separate temporary environment test variable.
3. Reopened the repaired print dialog with Work:OpenPrint.pdf (818 bytes)
   and explicit destination invalid, while the saved default remained the HP.
4. Browsed/query-verified the HP ColorLaserJet M282-M285 using real IPP
   Get-Printer-Attributes. Selected the Confirmed row and clicked Use Printer.
5. The browser closed. The already-open print dialog changed its destination
   and displayed Selected ipp://192.168.0.6:631/ipp/print. It stayed responsive.
6. No Print button was pressed and no send-worker request was created. This
   is a selection-handoff pass, not a new physical-print acceptance claim.

The shared listener code is also integrated into OpenView. Its listener
build and native message semantics were checked; this incident's visual
end-to-end check was on the OpenPrint dialog, not every viewer action.

## Repeatable test

Build with ./build-amiga.sh, ./build-viewer.sh and ./build-browser.sh.
Run make test test-discovery test-submit for the existing regression gates.
C:OAPSelectionSmoke writes Work:OAPSelectionSmoke.txt and performs no network
operation or print submission. Its separate namespace never notifies normal
print/viewer windows or changes the real OpenPrint printer preference.

For the UI regression, open the dialog with an explicit invalid URI, Browse,
select the same confirmed printer that is already saved, then Use Printer.
The invalid URI must be replaced without Print, refocusing, or reopening.
