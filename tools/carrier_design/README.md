# OSPTEK carrier draft generator

This creates an **unrouted engineering draft**, never manufacturing outputs.
The project status in `display_board/hardware/display-carrier/DESIGN_STATUS.md`
is authoritative about incomplete circuits and validation holds.

Run on this workspace with KiCad 10.0.5's Python and the installed KiSkill engine:

```powershell
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/carrier_design/pcb.py
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
and actual PCB pads; checks all references and pad sets; verifies that the 22
pending-interface warnings have not changed unexpectedly; and computes USB and
NTC tolerance bounds. It deliberately records 499 unconnected items and a
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
