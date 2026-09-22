# OSPTEK carrier draft generator

This creates an **unrouted engineering draft**, never manufacturing outputs.
The project status in `display_board/hardware/display-carrier/DESIGN_STATUS.md`
is authoritative about incomplete circuits and validation holds.

Run on this workspace with KiCad 10.0.5's Python and the installed KiSkill engine:

```powershell
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/carrier_design/pcb.py --preserve-placement display_board/hardware/display-carrier/display_carrier.kicad_pcb
& 'C:/Program Files/KiCad/10.0/bin/kicad-cli.exe' sch erc --format json --output build/carrier-design/erc-final.json build/carrier-design/project/display_carrier.kicad_sch
& 'C:/Program Files/KiCad/10.0/bin/kicad-cli.exe' pcb drc --schematic-parity --format json --output build/carrier-design/drc-final.json build/carrier-design/project/display_carrier.kicad_pcb
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/carrier_design/verify.py
```

`cad.py` writes typed symbols, explicit no-connects, deterministic schematic UUIDs,
and verifies every schematic stub before saving. `board_spec.py` contains the
pin-level circuit source. `pcb.py` exports the native schematic netlist and uses
it to set actual PCB pad nets and hierarchical paths; it does not route copper.

The output always goes to `build/carrier-design/project/`. It does not overwrite
the checked-in engineering project or the historical bare-P4 board. Regeneration
replaces the scratch placement; **do not use it to overwrite manual routing**.

First generation imported the vendor Altium PcbLib and LCSC components. When
those scratch downloads are absent, the generator uses checked-in local Carrier
footprints and models. Native KiCad libraries and the local KiSkill installation
at `C:/projects/kicad-projects/tools/KiSkill-main` remain runtime requirements;
this script is not a platform-independent packaging tool.

`verify.py` independently compares the generated intent, native schematic XML
and actual PCB pads; checks all references and pad sets; verifies the explicitly
listed pending-interface warnings; and computes USB and NTC tolerance bounds.
It counts unrouted connections with the native connectivity API (the CLI DRC
report list can truncate at 499 entries) and records a
non-release status. A passing script does **not** mean ERC/DRC or product release
has passed in full.

Review all rendered schematic sheets, board faces and mechanical interfaces
after editing. Recheck project locks before promoting files. Copy only reviewed
CAD, local libraries and concise evidence into the formal project; raw render,
netlist and report outputs stay under `build/`.

`models.py` stages reviewed project-local footprints and 3D bindings from
`model-manifest.json`; do not replace these with the supplier footprints during
regeneration. `check_models.py --project <dir> --output <report.json>` checks
physical-component coverage, project/library bindings, transforms and hashes.
Optional `--baseline <dir>` proves all native CAD geometry/electrical nodes are
unchanged except model bindings and the documented footprint-library remaps.
The checker requires the same native KiCad Python and local KiSkill parser.

`generate_reference_models.py --output <scratch-dir>` runs with CadQuery 2.6+
to rebuild the four explicitly marked dimensioned reference models. Their
source dimensions and limitations are in the project's `3D_MODELS.md`.
Supplier model files and the existing OSPTEK model are not regenerated.

`fpc_interfaces.py` adds the three actual FPC sockets and their documented pin
maps. LCD logic voltage conversion is implemented; panel bias/backlight and
camera support remain explicit open boundaries, not fabricated connections.
The six-pin touch interface is a reference pending the actual touch drawing.

For additive changes, pass `pcb.py --preserve-placement <existing.kicad_pcb>`
to keep old positions while placing new parts. Use `--library-source <dir>` to
stage a reviewed library before promotion. A generated board still has new
internal footprint IDs, so do not promote it wholesale over an existing board.
`stage_additions.py --baseline <project> --generated <scratch> --output <stage>`
keeps existing PCB identities/geometry/copper, adds new footprints, and allows
only assembly flags and sheet/status metadata to change on old footprints.
It refuses deleted refs, moved parts or changed existing pad nets. Verify that
staged project with `verify.py --project <stage> --evidence <reports>`, after
exporting `netlist.xml`, `erc-final.json` and `drc-final.json` into the report
directory. Keep the user's project settings when promoting.

The 2026-09-23 revision adds `cabin_sensors.py` (OPT3001 0x45 and SHT40 0x44).
`sensor_assets.py --output <scratch-library>` builds the reviewed optical land
pattern and two explicitly labelled body-envelope models, and copies the
official SHT4x land pattern with its central copper keepout.

`mechanical_layout.py --project <newly-staged-project>` applies the corrected
7.5/46.1 mm folded-tip dimensions, using the documented screen offset and
nominal connector insertion. Start from the pre-layout `a28a2e7` checkpoint
for this one-time reflow; it refuses an already reflowed or routed project.
It moves existing identities in place, records every change in
`MECHANICAL_LAYOUT.json`, adds flex rule areas and RH/T isolation slots,
and updates the staged `design-intent.json` to match actual coordinates/faces.
Touch X is a photo estimate; mechanical mating is not signed off.

`render_layout.py --project <reviewed-project>` regenerates `FLOORPLAN.svg`
and `FPC_FIT.svg` from the actual PCB. Full revision-G evidence is under
`build/carrier-mechanical/`; release boundaries are recorded in
`FPC_FIT_REVIEW.md`, `CABIN_SENSORS.md`, and `DESIGN_STATUS.md`.

For later additive changes, `--preserve-placement` also retains the face of
flipped parts. Stage against the current project to preserve the slots/rule
areas; do not replace its board with the generator's bare rectangular board.

Revision H adds `stereo_audio.py`: ES8389 using the S31-Korvo-1 clock/data pin
multiplexing, two onboard LMA3729T381 microphones, onboard RDA5807M and two
NS4150B amplifiers. `verify.py` independently checks these pin maps, the 0x12
codec address straps, separate FM channels, speaker polarity, 60 mm spacing,
and the 4.788 V PA rail tolerance. It does not validate firmware or sound.

`stage_audio.py --baseline <project> --library <reviewed-library> --output <stage>`
replaces only audio-sheet footprints on an **unrouted** board. It retains all
other PCB objects, the project rules and existing reference identities; it
preserves screen-flex/thermal geometry while placing audio parts. Review its
`AUDIO_LAYOUT.json` and run native ERC/DRC, netlist verification and model checks
before promotion. It also writes temporary schematic source into the normal
`build/carrier-design/project/` directory, which must not be used as a release.

`audio_assets.py --output <scratch-library>` was used for the first migration:
it expects the C22380673 supplier download in
`build/carrier-audio/es8389-part/libs/`, repairs its courtyard and creates the
manufacturer-dimensioned microphone footprint and reference model. The reviewed
assets are checked into the formal project, so normal regeneration should use
those local assets instead of fetching/recreating them.

The original region placer now requires `--preserve-placement` for revision H.
It reproduces schematic intent and positions for checking but does not preserve
the reviewed board's UUIDs, slots, rule areas or full layout. Use the staged
workflow for CAD promotion. Current sources, firmware migration requirements
and verification boundaries are in `AUDIO_ARCHITECTURE.md`; native evidence is
under `build/carrier-audio/`.

Revision I replaces the hinged SIM8060 with GCT SIM8066-6-1-14-01-A, a
right-edge push-push Nano-SIM socket. `sim_pushpush.py --baseline <project>
--output <stage>` is the one-time migration for an unrouted revision-H board:
it expects the C3032925 download in `build/carrier-sim/sim8066-part/libs/`,
rebuilds its reviewed NPTH/land pattern/copper keepout, preserves all other
PCB objects, updates C1030/R1033 and adds the SIM DATA pull-up R1034. It uses
the normal generator scratch directory; neither scratch output is a release.

Normal subsequent generation uses the checked-in SIM8066 footprint/model
through `--library-source` and `--preserve-placement`, without downloading
supplier data again. Native checks plus the independent verifier cover NC
card-detect polarity, SIM-domain pull-ups, pin maps, peg holes, shell grounds
and socket orientation. Review current-revision drawing changes and physical
card/enclosure operation before manufacturing; see `SIM_SOCKET.md`.

Revision J selects Cjiang FEXU0640S2R2MGS (C54140025) for L400. For the
one-time unrouted migration, fetch C54140025 with `kx fetch` into
`build/carrier-inductor/cjiang0640-part`, then run native KiCad Python with
`compact_inductor.py --baseline <reviewed-project> --output <scratch-project>`.
The script rebuilds the manufacturer's exact 2.25 x 6.30 mm lands with a
2.00 mm gap, seats the supplier STEP at Z +0.10 mm, and preserves every
other PCB object. Normal later regeneration uses the checked local library
and model manifest; no download is required. Run native ERC, DRC/parity,
netlist export, `verify.py`, `check_models.py`, and render/review before
promotion. `INDUCTOR_SELECTION.md` records procurement, height and electrical
limits; the part change does not qualify the converter for continuous 4 A.
