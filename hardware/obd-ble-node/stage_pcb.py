"""Stage a linked, net-assigned PCB core. J1 and final routing await mechanical data.

Runs an exhaustive schematic netlist vs design.json audit before staging. Uses
native KiCad PCB API; generated files are openable in KiCad without an add-on.
"""
import argparse, json, os, subprocess, sys
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('--engine',required=True)
parser.add_argument('--libraries',default=r'C:\Program Files\KiCad\10.0\share\kicad')
args=parser.parse_args();sys.path.insert(0,args.engine)
from kicad_lib import sexp
import pcbnew as k
ROOT=Path(__file__).resolve().parent; NAME='obd_ble_node'
config=json.loads((ROOT/'design.json').read_text(encoding='utf-8'))
subprocess.run(['kicad-cli','sch','export','netlist','--output',str(ROOT/'build/netlist.net'),str(ROOT/(NAME+'.kicad_sch'))],check=True)
tree=sexp.load_file(str(ROOT/'build/netlist.net'))
def field(node,name):
    f=sexp.find(node,name);return sexp.atoms(f)[0] if f is not None else ''
comps={field(c,'ref'):c for c in sexp.walk(tree,'comp')}
nets={}; pin_net={}
for n in sexp.walk(tree,'net'):
    name=field(n,'name'); entries=[]
    for node in sexp.find_all(n,'node'):
        member=(field(node,'ref'),field(node,'pin')); entries.append(member);pin_net[member]=name
    nets[name]=set(entries)
for name,members in config['expected_nets'].items():
    actual=nets.get('/'+name,nets.get(name,set()))
    assert actual==set(map(tuple,members)), (name,actual.symmetric_difference(set(map(tuple,members))))
board=k.BOARD();board.SetCopperLayerCount(4)
settings=board.GetDesignSettings();settings.SetCopperLayerCount(4)
settings.m_TrackMinWidth=k.FromMM(.15);settings.m_MinClearance=k.FromMM(.15)
settings.m_CopperEdgeClearance=k.FromMM(.25)
def v(x,y): return k.VECTOR2I(k.FromMM(x),k.FromMM(y))
for a,b in [((100,100),(138.5,100)),((138.5,100),(138.5,116.6)),((138.5,116.6),(100,116.6)),((100,116.6),(100,100))]:
    line=k.PCB_SHAPE();line.SetShape(k.SHAPE_T_SEGMENT);line.SetLayer(k.Edge_Cuts)
    line.SetStart(v(*a));line.SetEnd(v(*b));line.SetWidth(k.FromMM(.05));board.Add(line)
net_items={}
for index,name in enumerate(sorted(nets),1):
    info=k.NETINFO_ITEM(board,name,index);board.Add(info);net_items[name]=info
placed=[]
for p in config['parts']:
    if not p['footprint']: assert p['ref']=='J1';continue
    group,name=p['footprint'].split(':')
    library=Path(args.libraries)/'footprints'/(group+'.pretty')
    assert (library/(name+'.kicad_mod')).is_file(),p['footprint']
    fp=k.FootprintLoad(str(library),name);assert fp is not None
    fp.SetReference(p['ref']);fp.SetValue(p['value'])
    for prop_name,prop_text in [('MPN',p['mpn']),('DesignNote',p['note'])]:
        prop=k.PCB_FIELD(fp,k.FIELD_T_USER,prop_name);prop.SetText(prop_text)
        prop.SetVisible(False);prop.SetLayer(k.F_Fab);fp.Add(prop)
    fp.SetFPID(k.LIB_ID(group,name));board.Add(fp)
    path=k.KIID_PATH()
    for token in field(comps[p['ref']],'tstamps').strip('/').split('/'):
        path.push_back(k.KIID(token))
    fp.SetPath(path)
    fp.SetPosition(v(*p['pcb']));fp.SetOrientationDegrees(p['angle'])
    if p['side']=='B': fp.Flip(fp.GetPosition(),k.FLIP_DIRECTION_LEFT_RIGHT)
    fp.Value().SetVisible(False)
    fp.Reference().SetLayer(k.B_Fab if p['side']=='B' else k.F_Fab)
    size=.8 if p['ref'].startswith('U') else .45
    fp.Reference().SetTextSize(v(size,size));fp.Reference().SetTextThickness(k.FromMM(.08))
    fp.Reference().SetPosition(fp.GetPosition())
    for item in list(fp.GraphicalItems()):
        if isinstance(item,k.PCB_TEXT) and item.GetText() in ['${REFERENCE}','%R']:
            fp.Remove(item) # one assembly ref per part; hidden PCB_TEXT is not serialized in KiCad 10
    for pad in fp.Pads():
        number=pad.GetNumber()
        if not number:continue # paste-only apertures
        net=pin_net.get((p['ref'],number))
        if net:pad.SetNet(net_items[net])
        else: assert number not in p['nets'],(p['ref'],number)
    placed.append(p['ref'])
def note(s,x,y,size,layer):
    t=k.PCB_TEXT(board);t.SetText(s);t.SetPosition(v(x,y));t.SetTextSize(v(size,size));t.SetTextThickness(k.FromMM(.12));t.SetLayer(layer);board.Add(t)
note('38.5 x 16.6 mm / CORE PLACEMENT ONLY',119.25,97.2,.85,k.Dwgs_User)
note('J1 solder tails: footprint / holes / keepouts UNKNOWN',119.25,119.5,.8,k.Dwgs_User)
note('DO NOT FABRICATE / UNROUTED',119.25,121,.8,k.Dwgs_User)
note('OBD-C3 A',104.5,110,.8,k.F_SilkS)
k.SaveBoard(str(ROOT/(NAME+'.kicad_pcb')),board,True)
report={'net_membership':'PASS: every specified net and pin matches exported schematic','nets_audited':len(config['expected_nets']),
    'components_staged':len(placed),'omitted_components':['J1: actual connector solder-tail geometry unknown'],
    'board_mm':[38.5,16.6],'copper_layers':4,'routed':False}
(ROOT/'build/netlist-audit.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
