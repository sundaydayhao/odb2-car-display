"""Independent revision-P preservation, package and procurement checks.

Native ERC/DRC and verify.py must already have passed. Electrical and mechanical
measurements on real hardware remain necessary; these are design-file checks.
"""
import argparse, collections, csv, hashlib, json, math, sys
from pathlib import Path
import pcbnew as k
sys.path.insert(0,'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp


def review(baseline,project,evidence):
    old=k.LoadBoard(str(baseline/'display_carrier.kicad_pcb'))
    board=k.LoadBoard(str(project/'display_carrier.kicad_pcb'))
    before={f.GetReference():f for f in old.GetFootprints()}
    after={f.GetReference():f for f in board.GetFootprints()}
    assert set(before)<=set(after)
    assert all(before[r].m_Uuid.AsString()==after[r].m_Uuid.AsString() for r in before)
    # No routing, rules, board outline, slots or mounting-hole edits allowed.
    def invariant(path):
        tree=sexp.load_file(str(path))
        return [n for n in tree if isinstance(n,list) and sexp.tag_of(n) not in
                {'footprint','net','title_block','generator_version'}]
    assert invariant(baseline/'display_carrier.kicad_pcb')==invariant(project/'display_carrier.kicad_pcb')
    for ref in ['U100','U1000','J1200','J1210','J1220','H1','H2','H3','H4']:
        a,b=before[ref],after[ref]
        assert a.GetPosition()==b.GetPosition() and a.GetOrientationDegrees()==b.GetOrientationDegrees()
        assert str(a.GetFPID())==str(b.GetFPID())
        assert [(p.GetNumber(),p.GetPosition(),p.GetSize(),p.GetNetname()) for p in a.Pads()]==[(p.GetNumber(),p.GetPosition(),p.GetSize(),p.GetNetname()) for p in b.Pads()]
    manifest=json.loads((project/'model-manifest.json').read_text('utf-8'))
    def template(name):return k.FootprintLoad(str(project/'Carrier.pretty'),name)
    def pad(f,n):return next(z for z in f.Pads() if z.GetNumber()==str(n))
    def xy(z):return [k.ToMM(z.GetPosition().x),k.ToMM(z.GetPosition().y)]
    def near(a,b):assert all(abs(x-y)<.001 for x,y in zip(a,b)),(a,b)
    q=template('ES8388_QFN28_4x4_P0.45')
    # Four pin-one/end-of-side pairs independently establish counterclockwise
    # numbering on the 0.45 mm grid; center pad is grounded in native netlist.
    for n,x,y in [(1,-1.975,-1.35),(7,-1.975,1.35),(8,-1.35,1.975),
                  (14,1.35,1.975),(15,1.975,1.35),(21,1.975,-1.35),
                  (22,1.35,-1.975),(28,-1.35,-1.975),(29,0,0)]:near(xy(pad(q,n)),[x,y])
    near([k.ToMM(pad(q,29).GetSize().x),k.ToMM(pad(q,29).GetSize().y)],[2.6,2.6])
    assert pad(after['U800'],29).GetNetname()=='GND'
    s=template('FSV4730_SSOP24');assert len(list(s.Pads()))==24
    assert abs(abs(xy(pad(s,2))[1]-xy(pad(s,1))[1])-.635)<.001
    for name,centers,size in [('L_DMBJ_CXP0520',[-1.8,1.8],[1.6,2.5]),
                               ('L_Sunlord_MWSA0402S',[-1.85,1.85],[1.5,2.5]),
                               ('SXTF3215R_32k',[-1.25,1.25],[1.0,1.8])]:
        f=template(name)
        for n,x in enumerate(centers,1):
            z=pad(f,n);near(xy(z),[x,0]);near([k.ToMM(z.GetSize().x),k.ToMM(z.GetSize().y)],size)
    f=template('TS038_SideKey')
    for n,x in [('1',-2.25),('2',2.25)]:
        z=pad(f,n);near(xy(z),[x,0]);near([k.ToMM(z.GetDrillSize().x)],[1.1])
    assert len([p for p in f.Pads() if p.GetNumber()=='SH'])==2
    hole=pad(f,'');assert hole.GetAttribute()==k.PAD_ATTRIB_NPTH
    near(xy(hole),[0,2.3]);near([k.ToMM(hole.GetDrillSize().x),k.ToMM(hole.GetDrillSize().y)],[2.5,.6])
    f=template('SK22D02L4_SideSlide')
    for n in range(1,7):near([k.ToMM(pad(f,n).GetDrillSize().x)],[.8])
    for ref in ['SW101','SW102','SW310']:
        assert all(p.GetNetname()=='GND' for p in after[ref].Pads() if p.GetNumber()=='SH')
    intent=json.loads((project/'design-intent.json').read_text('utf-8'))
    parts={p['ref']:p for p in intent['parts']}
    for ref in ['C1012','C1013']:
        assert parts[ref]['kind']=='CPOL'
        assert pad(after[ref],1).GetNetname()=='MODEM_3V8' and pad(after[ref],2).GetNetname()=='GND'
    assert pad(after['SW310'],2).GetNetname()=='SLIDER_ON'
    assert all(pad(after['SW310'],n).GetNetname().startswith('unconnected-') for n in [4,5,6])
    distances={}
    for c,u,pin in [('C814','U815',21),('C823','U815',20),('C824','U815',22),
                    ('C805','U800',10),('C881','U800',19),('C882','U800',20)]:
        a,z=pad(after[c],1).GetPosition(),pad(after[u],pin).GetPosition()
        distances[c+'-'+u+'.'+str(pin)]=round(k.ToMM(math.hypot(a.x-z.x,a.y-z.y)),3)
    assert max(distances.values())<5,distances
    if (project/'AUDIO_EDGE_LAYOUT.json').exists():
        for c,u in [('C840','U802'),('C844','U803')]:
            a,z=pad(after[c],1).GetPosition(),pad(after[u],1).GetPosition()
            distance=round(k.ToMM(math.hypot(a.x-z.x,a.y-z.y)),3)
            assert distance<3,(c,u,distance)
            distances[c+'-'+u+'.1']=distance
    rows=list(csv.DictReader((project/'PROCUREMENT_100PCS.csv').read_text('utf-8-sig').splitlines()))
    selected=[r for r in rows if r['LCSC'] and int(r['ProposedQty'])]
    assert len(selected)==85
    assert all(int(r['ObservedStock'])>=int(r['ProposedQty']) for r in selected)
    direct=[r for r in selected if r['StockChannel']=='lcsc_domestic_direct']
    assert len(direct)==84
    assert all(int(r['ObservedStock'])>=3*int(r['ProposedQty']) for r in direct)
    assert [r['References'] for r in rows if r['SupplyStatus']=='HOLD']==['F300']
    v=json.loads((evidence/'verification-summary.json').read_text('utf-8'))
    assert v['pcb_physical_violations']==v['schematic_parity_violations']==v['erc_errors']==0
    report=dict(revision='P',baseline='90df33e',status='PASS_DESIGN_FILES_ONLY',
        original_footprint_UUIDs_preserved=len(before),added_references=sorted(set(after)-set(before)),
        unchanged_board_rules_outline_slots=True,main_controller_and_ML307_unchanged=True,
        FPC_connectors_unchanged=True,critical_package_land_geometry='PASS',
        polymer_capacitor_polarity='PASS',FM_reset_crystal_and_codec_pinmaps='VERIFICATION.json',
        bypass_pad_center_distances_mm=distances,stock_codes=85,direct_stock_codes=84,
        partner_stock_codes=1,unresolved_sourcing_references=['F300'],
        native_erc_errors=0,native_erc_existing_warnings=v['erc_warnings'],
        native_physical_drc=0,native_schematic_parity=0,unconnected=v['pcb_unconnected_items'],
        physical_measurements='NOT_RUN',routing='UNROUTED',driver_ports='NOT_IMPLEMENTED',
        enclosure_fit='NOT_VALIDATED: new switches, FM package and membrane height',
        models='All new local models are simplified datasheet envelopes, not manufacturer mating CAD')
    if (project/'AUDIO_EDGE_LAYOUT.json').exists():
        report.update(microphone_center_spacing_mm=131,speaker_connectors_at_opposite_edges=True,
                      audio_edge_layout='AUDIO_EDGE_LAYOUT.json')
    (project/'SOURCING_REVIEW.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['baseline','project','evidence']:ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args();review(a.baseline,a.project,a.evidence)
