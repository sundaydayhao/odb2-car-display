"""Generate reviewable KiCad 10 sheets from an explicit, pin-complete circuit model.

This is an engineering source, not an autorouter or a fabrication sign-off.
The KiSkill geometric verifier runs before each generated schematic is saved.
"""
from pathlib import Path
import copy
import json
import math
import sys
import uuid

sys.path.insert(0, 'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp
from kicad_lib.verify import Seg, Pin, verify

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/carrier-design/project'
LIBS = Path('C:/Program Files/KiCad/10.0/share/kicad')
NS = uuid.UUID('886fb697-d6e9-49c3-af53-65051cdc22dc')
PROJECT = 'display_carrier'
ROOT_ID = str(uuid.uuid5(NS, 'root'))
def uid(key): return str(uuid.uuid5(NS, key))
def q(value): return json.dumps(str(value), ensure_ascii=False)
def snap(v): return round(round(v / 1.27) * 1.27, 3)
def f(v): return f'{v:.4f}'.rstrip('0').rstrip('.')

class Design:
    def __init__(self):
        self.symbols = {}
        self.parts = []
        self.sheets = {}
        self.notes = {}
        self.geom_results = {}

    def box_symbol(self, name, pins, halfwidth=15.24):
        """pins = [(number, name, electrical_type)]; library pins remain typed."""
        n = math.ceil(len(pins)/2)
        halfheight = max(5.08, snap((n+1)*1.27))
        geometries = {}
        b = [f'(symbol {q(name)} (pin_names (offset 0.635)) (in_bom yes) (on_board yes)',
             f'(property "Reference" "U" (at 0 {f(halfheight+2.54)} 0) (effects (font (size 1.27 1.27))))',
             f'(property "Value" {q(name)} (at 0 {f(-halfheight-2.54)} 0) (effects (font (size 1.27 1.27))))',
             f'(symbol {q(name+"_0_1")} (rectangle (start {-halfwidth} {halfheight}) (end {halfwidth} {-halfheight}) (stroke (width 0.254) (type default)) (fill (type background))))',
             f'(symbol {q(name+"_1_1")}']
        for i,(number,label,typ) in enumerate(pins):
            side = -1 if i<n else 1
            y = snap((n-1)*1.27-(i % n)*2.54)
            x = side*(halfwidth+5.08)
            a = 0 if side<0 else 180
            geometries[str(number)] = (round(x,3),round(-y,3),side,typ)
            b.append(f'(pin {typ} line (at {f(x)} {f(y)} {a}) (length 5.08) (name {q(label)} (effects (font (size 1.016 1.016)))) (number {q(number)} (effects (font (size 1.016 1.016)))))')
        b.append('))')
        self.symbols[name] = {'tree':sexp.parse('\n'.join(b)), 'pins':geometries,'halfheight':halfheight,'halfwidth':halfwidth}

    def passive_symbols(self):
        for name in ['R','C','L','D','Q_NMOS','Q_NPN','SW_SPST','TestPoint','PWR_FLAG']:
            if name in ['R','C','L','D']:
                # Use the native official graphics; retain exact numbered pins.
                self.official(name, 'Device', name)
            elif name=='Q_NMOS': self.official(name,'Transistor_FET','2N7002')
            elif name=='Q_NPN': self.official(name,'Transistor_BJT','MMBT3904')
            elif name=='SW_SPST': self.official(name,'Switch','SW_SPST')
            elif name=='TestPoint': self.official(name,'Connector','TestPoint')
            else: self.official(name,'power','PWR_FLAG')

    def official(self, newname, library, original):
        doc = sexp.load_file(str(LIBS/'symbols'/f'{library}.kicad_sym'))
        syms={sexp.atoms(s)[0]:s for s in sexp.find_all(doc,'symbol')}
        def flatten(name):
            d=copy.deepcopy(syms[name]); e=sexp.find(d,'extends')
            if e:
                base=flatten(sexp.atoms(e)[0]); oldname=sexp.atoms(base)[0]
                overrides={sexp.atoms(x)[0]:x for x in sexp.find_all(d,'property')}
                base=[x for x in base if not (isinstance(x,list) and sexp.tag_of(x)=='property' and sexp.atoms(x)[0] in overrides)]
                base.extend(overrides.values()); d=base
            old=sexp.atoms(d)[0];d[1]=sexp.QStr.of(name)
            for sub in sexp.find_all(d,'symbol'):
                sub[1]=sexp.QStr.of(sexp.atoms(sub)[0].replace(old+'_', name+'_',1))
            return d
        d=flatten(original);old=sexp.atoms(d)[0];d[1]=sexp.QStr.of(newname)
        for prop in sexp.find_all(d,'property'):
            if sexp.atoms(prop)[0]=='Value':prop[2]=sexp.QStr.of(newname)
        pins={};h=5.08;w=5.08
        for sub in sexp.find_all(d,'symbol'):
            sub[1]=sexp.QStr.of(sexp.atoms(sub)[0].replace(old+'_',newname+'_',1))
            for p in sexp.find_all(sub,'pin'):
                number=sexp.atoms(sexp.find(p,'number'))[0]
                x,y,a=map(float,sexp.atoms(sexp.find(p,'at'))[:3])
                side={0:-1,180:1,90:2,270:-2}[int(a)]
                pins[number]=(x,-y,side,sexp.atoms(p)[0]);h=max(h,abs(y));w=max(w,abs(x))
        self.symbols[newname]={'tree':d,'pins':pins,'halfheight':h,'halfwidth':w}

    def sheet(self, name, title, notes=()):
        self.sheets[name]=title;self.notes[name]=list(notes)

    def add(self, sheet, ref, kind, value, nets, xy, footprint='', pcb=None, side='B', angle=0, status='engineering', source='', dnp=False):
        spec=self.symbols[kind];nets={str(k):v for k,v in nets.items()}
        assert set(nets)==set(spec['pins']), (ref,set(spec['pins'])-set(nets),set(nets)-set(spec['pins']))
        assert ref not in [p['ref'] for p in self.parts],ref
        p=dict(sheet=sheet,ref=ref,kind=kind,value=value,nets=nets,xy=[snap(x) for x in xy],footprint=footprint,pcb=pcb,side=side,angle=angle,status=status,source=source,dnp=dnp)
        self.parts.append(p);return p

    def write(self):
        OUT.mkdir(parents=True,exist_ok=True)
        # One mutation check ensures the verifier rejects an actual cross-net short.
        assert verify([Seg('A',(0,1.27),(2.54,1.27)),Seg('B',(1.27,0),(1.27,2.54))],[],[],False)
        root=[f'(kicad_sch (version 20250114) (generator "eeschema") (uuid {ROOT_ID}) (paper "A3") (lib_symbols)',
              '(title_block (title "OSPTEK P4C5 / P4C6 Display Carrier") (date "2026-09-23") (rev "R0I SIM") (company "OBD Car Display") (comment 1 "NOT FOR FABRICATION - assembly and validation holds apply"))']
        for i,(name,title) in enumerate(self.sheets.items()):
            sid=uid('sheet:'+name);x=15.24+(i%4)*100.33;y=35.56+(i//4)*45.72
            root.append(f'(sheet (at {x} {y}) (size 91.44 33.02) (stroke (width 0.254) (type default)) (fill (color 0 0 0 0)) (uuid {sid}) (property "Sheetname" {q(title)} (at {x} {y-1.27} 0) (effects (font (size 1.27 1.27)) (justify left bottom))) (property "Sheetfile" {q(name+".kicad_sch")} (at {x} {y+34.29} 0) (effects (font (size 1.016 1.016)) (justify left top))) (instances (project {q(PROJECT)} (path {q("/"+ROOT_ID)} (page {q(i+2)})))))')
            self.write_sheet(name,title,sid,i+2)
        root.append(f'(sheet_instances (path "/" (page "1"))) (embedded_fonts no))')
        doc=sexp.parse('\n'.join(root));sexp.save_file(str(OUT/(PROJECT+'.kicad_sch')),doc)
        defs=[sexp.dumps(v['tree']) for v in self.symbols.values()]
        sexp.save_file(str(OUT/'Carrier.kicad_sym'),sexp.parse('(kicad_symbol_lib (version 20250114) (generator "kicad_symbol_editor")\n'+'\n'.join(defs)+')'))
        (OUT/'sym-lib-table').write_text('(sym_lib_table (lib (name "Carrier") (type "KiCad") (uri "${KIPRJMOD}/Carrier.kicad_sym") (options "") (descr "Pin-reviewed carrier parts")))\n',encoding='utf-8')
        (OUT/'design-intent.json').write_text(json.dumps({'root_uuid':ROOT_ID,'parts':self.parts,'sheets':self.sheets,'geometric_verifier':self.geom_results},indent=2,ensure_ascii=False)+'\n',encoding='utf-8')

    def write_sheet(self,name,title,sid,page):
        parts=[p for p in self.parts if p['sheet']==name]
        used={p['kind'] for p in parts};defs=[]
        for kind in sorted(used):
            d=copy.deepcopy(self.symbols[kind]['tree']);d[1]=sexp.QStr.of('Carrier:'+kind);defs.append(sexp.dumps(d))
        sheetid=uid('doc:'+name)
        date='2026-09-23' if name in ['10_sim_rf','11_codec','12_fm_audio','13_release_holds','17_cabin_sensors','18_dual_mic','19_stereo_pa'] else '2026-09-19'
        b=[f'(kicad_sch (version 20250114) (generator "eeschema") (uuid {sheetid}) (paper "A3")',
           f'(title_block (title {q(title)}) (date "{date}") (rev "R0 ENGINEERING") (company "OBD Car Display") (comment 1 "Prototype drawing; see DESIGN_STATUS.md"))',
           '(lib_symbols\n'+'\n'.join(defs)+')']
        segs=[];pinmodel=[];seen=set()
        for p in parts:
            ref=p['ref'];kind=p['kind'];s=self.symbols[kind];x,y=p['xy'];u=uid('part:'+ref)
            b.append(f'(symbol (lib_id {q("Carrier:"+kind)}) (at {f(x)} {f(y)} 0) (unit 1) (in_bom {"no" if ref.startswith(("#","TP")) else "yes"}) (on_board {"yes" if p["footprint"] else "no"}) (dnp {"yes" if p["dnp"] else "no"}) (uuid {u})')
            vertical = kind in ['R','C','L','FUSE','PWR_FLAG','Q_NMOS','Q_NPN']
            rx,ry=(x+5.08,y-1.27) if vertical else (x,y-s['halfheight']-5.08)
            vx,vy=(x+5.08,y+1.27) if vertical else (x,y-s['halfheight']-2.54)
            if kind=='USBLC6':
                rx,ry=x,y-20.32
                vx,vy=x,y-17.78
            for prop,value,px,py,hide in [('Reference',ref,rx,ry,False),('Value',p['value'],vx,vy,False),('Footprint',p['footprint'],x,y,True),('Datasheet',p['source'],x,y,True),('DesignStatus',p['status'],x,y,True)]:
                b.append(f'(property {q(prop)} {q(value)} (at {f(px)} {f(py)} 0) (effects (font (size {1.016 if prop=="Value" else 1.27} {1.016 if prop=="Value" else 1.27}))'+(' (justify left)' if vertical else '')+(' (hide yes)' if hide else '')+'))')
            for n in s['pins']:b.append(f'(pin {q(n)} (uuid {uid(ref+":"+n)}))')
            b.append(f'(instances (project {q(PROJECT)} (path {q("/"+ROOT_ID+"/"+sid)} (reference {q(ref)}) (unit 1)))))')
            for n,(dx,dy,side,typ) in s['pins'].items():
                px,py=round(x+dx,3),round(y+dy,3);net=p['nets'][n]
                if net is None:
                    b.append(f'(no_connect (at {f(px)} {f(py)}) (uuid {uid("nc:"+ref+":"+n)}))');continue
                end=(round(px+(5.08*side if abs(side)==1 else 0),3),round(py+(5.08*(side/2) if abs(side)==2 else 0),3))
                a=(px,py);key=(net,a,end)
                pinmodel.append(Pin(net,a,ref+'.'+n))
                if key in seen:continue
                seen.add(key);segs.append(Seg(net,a,end))
                b.append(f'(wire (pts (xy {f(px)} {f(py)}) (xy {f(end[0])} {f(end[1])})) (stroke (width 0) (type default)) (uuid {uid("wire:"+ref+":"+n)}))')
                # Global names are shared by all sheets, with a visible pin stub.
                rot=(180 if side<0 else 0) if abs(side)==1 else (0 if side<0 else 180)
                b.append(f'(global_label {q(net)} (shape bidirectional) (at {f(end[0])} {f(end[1])} {rot}) (effects (font (size 1.016 1.016)) (justify '+('left' if rot==0 else 'right')+f')) (uuid {uid("label:"+ref+":"+n)}))')
        violations=verify(segs,pinmodel,[],merge_power=False)
        assert not violations, (name,violations[:20])
        self.geom_results[name]={'segments':len(segs),'pins':len(pinmodel),'violations':0}
        for i,note in enumerate(self.notes[name]):
            b.append(f'(text {q(note)} (at 20.32 {f(20.32+i*5.08)} 0) (effects (font (size 1.27 1.27)) (justify left top)) (uuid {uid(name+":note:"+str(i))}))')
        b.append('(embedded_fonts no))')
        doc=sexp.parse('\n'.join(b));sexp.save_file(str(OUT/(name+'.kicad_sch')),doc)
