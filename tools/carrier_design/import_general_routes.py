"""Selectively import ordinary nets; preserve reviewed PCB objects and MIPI.

The supplied exclusion list must include all controlled-impedance, analog and
power-loop nets. Keep only routes that pass the native physical DRC; record all
removed nets. This is a partial-routing stage, not a completion claim.
"""
import argparse,collections,json,math,subprocess
from pathlib import Path
import pcbnew as k
from cad import sexp,LIBS

def main(project,session,exclusions,evidence):
    assert 'build' in project.resolve().parts
    file=project/'display_carrier.kicad_pcb';base=sexp.load_file(str(file))
    manual=set(json.loads(exclusions.read_text('utf8')))
    assert 'GND' in manual and all(n+'_'+p in manual for n in ['DSI_D0','DSI_D1','DSI_CLK','CSI_D0','CSI_D1','CSI_CLK'] for p in ['P','N'])
    b=k.LoadBoard(str(file));assert k.ImportSpecctraSES(b,str(session))
    imported=evidence/'general-import.kicad_pcb';k.SaveBoard(str(imported),b)
    tree=sexp.load_file(str(imported))
    def net(n):return str(sexp.find(n,'net')[1])
    incoming=[n for n in tree if isinstance(n,list) and sexp.tag_of(n) in ['segment','via','arc'] and net(n) not in manual]
    assert incoming,'No ordinary routes in session'
    # Reject any ordinary-net via whose antipad would cut underneath MIPI.
    mipi=[n for n in base if isinstance(n,list) and sexp.tag_of(n)=='segment' and net(n).startswith(('DSI_','CSI_'))]
    def point(n,key):return tuple(float(v) for v in sexp.find(n,key)[1:3])
    def distance(p,a,c):
        dx=c[0]-a[0];dy=c[1]-a[1];dd=dx*dx+dy*dy;t=min(1,max(0,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/dd)) if dd else 0
        return math.hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy)
    removed={}
    for n in incoming:
        if sexp.tag_of(n)!='via':continue
        radius=float(sexp.find(n,'size')[1])/2+.127+.056+.1
        if any(distance(point(n,'at'),point(t,'start'),point(t,'end'))<radius for t in mipi):removed[net(n)]='Reference-plane antipad would approach MIPI'
    for iteration in range(5):
        keep=[n for n in incoming if net(n) not in removed]
        sexp.save_file(str(file),base+keep)
        bb=k.LoadBoard(str(file));k.ZONE_FILLER(bb).Fill(bb.Zones());k.SaveBoard(str(file),bb)
        out=evidence/'drc-final.json'
        result=subprocess.run([str(LIBS.parents[1]/'bin/kicad-cli.exe'),'pcb','drc','--format','json','--schematic-parity','--severity-all','-o',str(out),str(file)],capture_output=True)
        assert out.exists(),result.stderr.decode(errors='replace')
        drc=json.loads(out.read_text('utf8'));assert not drc['schematic_parity']
        if not drc['violations']:break
        ids={str(sexp.find(n,'uuid')[1]):net(n) for n in keep};rejected=set()
        for v in drc['violations']:
            candidates={ids[x['uuid']] for x in v['items'] if x['uuid'] in ids}
            assert candidates,('Violation in preserved objects',v)
            for name in candidates:removed[name]=v['type'];rejected.add(name)
        assert rejected
    else:raise RuntimeError('Native DRC did not converge; do not promote this stage')
    report=dict(router='freerouting 2.4.1',scope='Ordinary nets only; existing footprints, MIPI and GND kept from pre-import board',
        incoming_copper_items=len(incoming),accepted_copper_items=len(keep),accepted_nets=sorted({net(n) for n in keep}),removed_nets=removed,
        native_physical_drc=0,schematic_parity=0,status='PARTIAL_ROUTING')
    (project/'GENERAL_ROUTING.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print('Accepted',len(keep),'items on',len(report['accepted_nets']),'ordinary nets; removed',len(removed),'nets')

if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__)
    for n in ['project','session','exclusions','evidence']:a.add_argument('--'+n,type=Path,required=True)
    x=a.parse_args();main(x.project,x.session,x.exclusions,x.evidence)
