"""Route only the A8 additions; keep all A7 tracks locked and intact.

Native KiCad Python: export, run freerouting externally, then import.
The ground plane under the horizontal DHT20 body is removed on both layers.
"""
from pathlib import Path
import copy, re, shutil, sys, uuid
import pcbnew as k
sys.path.insert(0,'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'display_board/hardware/fm-mono-switch'; W=ROOT/'build/fm-mono-switch'
pcb=P/'fm_mono_switch.kicad_pcb'
mode=sys.argv[1]
if mode=='export':
    b=k.LoadBoard(str(pcb))
    assert not list(b.Zones()),'Only run on the staged, unfilled A8 board.'
    for d in b.GetDrawings():
        if isinstance(d,k.PCB_TEXT) and d.GetTextHeight()<k.FromMM(.8):
            d.SetTextSize(k.VECTOR2I(k.FromMM(.8),k.FromMM(.8)))
    k.SaveBoard(str(pcb),b)
    shutil.copyfile(pcb,W/'a8-preroute.kicad_pcb')
    assert k.ExportSpecctraDSN(b,str(W/'a8-board.dsn'))
    dsn=(W/'a8-board.dsn').read_text()
    dsn=re.sub(r'\(net GND\s*\(pins[^)]*\)\s*\)', '(net GND)',dsn)
    (W/'a8-signal.dsn').write_text(dsn)
elif mode=='import':
    # Loading under a scratch name creates default project settings. Always load
    # alongside the real .kicad_pro to preserve the reviewed rules/net classes.
    shutil.copyfile(W/'a8-preroute.kicad_pcb',pcb)
    b=k.LoadBoard(str(pcb))
    assert k.ImportSpecctraSES(b,str(W/'a8-routed.ses'))
    k.SaveBoard(str(pcb),b)
    doc=sexp.load_file(str(pcb));base=sexp.load_file(str(W/'a7-fm_mono_switch.kicad_pcb'))
    for z in sexp.find_all(base,'zone'):
        z=copy.deepcopy(z)
        z[:]=[n for n in z if not isinstance(n,list) or sexp.tag_of(n) not in ['filled_polygon','fill_segments']]
        pts=sexp.find(sexp.find(z,'polygon'),'pts')
        # Preserve the original inset and extend the lower boundary by 22 mm.
        for xy in sexp.find_all(pts,'xy'):
            if float(xy[2])>85:xy[2]=sexp.Sym(str(float(xy[2])+22))
        doc.append(z)
    doc.append(sexp.parse(f'''(zone (net 0) (net_name "") (layers "F.Cu" "B.Cu")
      (uuid "{uuid.uuid4()}") (name "DHT20 body: reduce heat conduction")
      (hatch edge 0.5) (connect_pads (clearance 0)) (min_thickness 0.25)
      (keepout (tracks allowed) (vias allowed) (pads allowed) (copperpour not_allowed) (footprints allowed))
      (fill (thermal_gap 0.3) (thermal_bridge_width 0.3))
      (polygon (pts (xy 53.2 87.4) (xy 66.8 87.4) (xy 66.8 104.5) (xy 53.2 104.5))))'''))
    sexp.save_file(str(pcb),doc)
    b=k.LoadBoard(str(pcb));b.BuildConnectivity();k.ZONE_FILLER(b).Fill(b.Zones());k.SaveBoard(str(pcb),b)
else:raise SystemExit('Use export or import.')
print('A8 routing phase:',mode)
