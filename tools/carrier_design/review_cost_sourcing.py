"""Independent checks for the eight-reference, one-board purchasing revision."""
import argparse,copy,csv,json,hashlib
from pathlib import Path
import pcbnew as k
from cad import sexp
from review_routing import review_mipi_corners
from cost_sourcing_spec import CHANGES

def properties(n):return {str(p[1]):p for p in sexp.find_all(n,'property')}
def ref(n):return str(properties(n)['Reference'][2])

def review(baseline,project,evidence):
 a=sexp.load_file(str(baseline/'display_carrier.kicad_pcb'));b=sexp.load_file(str(project/'display_carrier.kicad_pcb'))
 # Includes every trace/via, filled plane, rule, board slot, outline and label.
 unchanged=lambda t:[n for n in t if sexp.tag_of(n)!='footprint']
 assert unchanged(a)==unchanged(b),'Unrelated PCB data changed'
 old={ref(f):f for f in sexp.find_all(a,'footprint')};new={ref(f):f for f in sexp.find_all(b,'footprint')}
 assert old.keys()==new.keys()
 for r,f in new.items():
  if r not in CHANGES:assert f==old[r],r
  else:
   for field in ['uuid','at','path']:assert sexp.find(f,field)==sexp.find(old[r],field),(r,field)
   pp={str(sexp.find(p,'uuid')[1]):p for p in sexp.find_all(old[r],'pad')}
   for p in sexp.find_all(f,'pad'):
    q=pp[str(sexp.find(p,'uuid')[1])]
    assert p[1]==q[1] and sexp.find(p,'net')==sexp.find(q,'net'),r
    if r not in ['C1012','C1013']:assert p==q,r
 for path in project.glob('*.kicad_sch'):
  prior=sexp.load_file(str(baseline/path.name));current=sexp.load_file(str(path))
  if path.name=='17_cabin_sensors.kicad_sch':
   for note in sexp.find_all(current,'text'):
    note[1]=sexp.QStr.of(str(note[1]).replace('SHT40-AD1B-R2 fixed 0x44','SHT40-AD1F-R2 fixed 0x44'))
  prior_syms={ref(s):s for s in sexp.find_all(prior,'symbol')}
  for s in sexp.find_all(current,'symbol'):
   r=ref(s)
   if r in CHANGES:
    pr=properties(s);op=properties(prior_syms[r])
    for key in ['Value','Footprint','Datasheet','DesignStatus']:pr[key][2]=copy.deepcopy(op[key][2])
  assert prior==current,path.name
 board=k.LoadBoard(str(project/'display_carrier.kicad_pcb'))
 corners=review_mipi_corners(board);assert corners['acute_corners']==0 and not corners['violations']
 cn=board.GetConnectivity();cn.Build(board);assert cn.GetUnconnectedCount(False)==519
 assert len(board.GetTracks())==1242
 fp=k.FootprintLoad(str((project/'Carrier.pretty').resolve()),'KNSCHA_177EC0013_7343')
 # Independent manufacturer mounting pattern: 8.6 outer span / 3.8 gap / 2.8 tall.
 pads=sorted(list(fp.Pads()),key=lambda x:x.GetNumber())
 assert [round(k.ToMM(p.GetPosition().x),3) for p in pads]==[-3.1,3.1]
 assert all((round(k.ToMM(p.GetSize().x),3),round(k.ToMM(p.GetSize().y),3))==(2.4,2.8) for p in pads)
 v=json.loads((evidence/'verification-summary.json').read_text('utf8'))
 assert v['pcb_physical_violations']==v['schematic_parity_violations']==v['erc_errors']==0
 rows=list(csv.DictReader((project/'PROCUREMENT_PER_BOARD.csv').read_text('utf-8-sig').splitlines()))
 intent=json.loads((project/'design-intent.json').read_text('utf8'));expected={p['ref'] for p in intent['parts'] if p['footprint'] and not p['ref'].startswith('TP') and not p['dnp']}-{'U100','U1000','F300'}
 refs=[r for x in rows for r in x['位号'].split()]
 assert set(refs)==expected and len(refs)==len(expected)==328
 assert len(rows)==len({r['商品编号'] for r in rows})==86
 assert all(int(r['数量'])==len(r['位号'].split()) for r in rows)
 assert list(rows[0])==['商品编号','型号','品牌','封装','位号','数量']
 stock=json.loads((project/'LCSC_STOCK_SNAPSHOT.json').read_text('utf8'))['parts']
 assert all(stock[r['商品编号']]['stock_channel']=='lcsc_domestic_direct' and stock[r['商品编号']]['stockNumber']>=int(r['数量']) for r in rows)
 # TLV62585 0.6V feedback +/-2%; 536k/100k +/-1% plus 1% drift allowance.
 p={x['ref']:x for x in intent['parts']};assert p['R1010']['value'].startswith('536k') and p['R1011']['value'].startswith('100k')
 vmax=.6*1.02*(1+536000*1.02/(100000*.98));assert vmax<4.05 and vmax/6.3<.65
 result=dict(revision='R',changed_references=sorted(CHANGES),board_count=1,upload_rows=86,upload_fitted_count=328,
   complete_fitted_count=331,excluded_fixed=['U100','U1000'],excluded_hold=['F300'],excluded_dnp_count=7,
   pcb_tracks_vias_fills_rules_outline_slots_unchanged=True,unchanged_footprints=365-len(CHANGES),
   all_pad_nets_uuids_and_component_centers_preserved=True,schematic_changes='Value, Footprint, Datasheet and DesignStatus on eight existing references; one SHT40 model-name note',
   mipi_corners=corners,remaining_unconnected=519,tracks_vias=1242,physical_drc=0,schematic_parity=0,erc_errors=0,erc_warnings=v['erc_warnings'],
   polymer_max_static_rail_v=round(vmax,4),polymer_static_voltage_utilization=round(vmax/6.3,4),
   polymer_land_pattern_mm=dict(outer=8.6,gap=3.8,pad=[2.4,2.8]),
   engineering_limits=['Polymer ripple rating not given in the linked datasheet; RF burst, startup and temperature qualification remain open.',
     'MLCC effective capacitance at bias and converter stability remain open.', 'SHT40 AD1B has no PTFE membrane; liquid/cleaning/coating protection is required.',
     'BQ25896RTWT domestic direct inventory observed 72; larger batches require another availability check.'],
   status='DESIGN_FILE_CHECKS_PASS_PARTIAL_BOARD_NOT_RELEASED')
 (project/'COST_REVIEW.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps({k:v for k,v in result.items() if k!='mipi_corners'},ensure_ascii=False,indent=2))

if __name__=='__main__':
 p=argparse.ArgumentParser()
 for n in ['baseline','project','evidence']:p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();review(a.baseline,a.project,a.evidence)
