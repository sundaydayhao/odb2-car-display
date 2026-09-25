"""Independent native-data audit of passive shrink and the partial routing.

Checks saved tracks, pads, filled polygons and source identities, not only the
router's intended paths. Does not establish signal integrity on real hardware.
"""
import argparse,collections,csv,json,math,sys
from pathlib import Path
import pcbnew as k
from cad import sexp

def review_mipi_corners(board):
    """Measure saved copper junctions, including joins between path chunks.

    DRC clearance and length checks alone accepted the old 13.5/55.5 degree
    phase-compensation tips. Gate MIPI centerlines at 135 degrees or wider
    (at most a 45 degree heading change), allowing native rounding tolerance.
    Via transitions are separate junctions on each layer, not planar corners.
    """
    nodes=collections.defaultdict(list)
    for t in board.GetTracks():
        if isinstance(t,k.PCB_VIA) or not t.GetNetname().startswith(('DSI_','CSI_')):continue
        assert not isinstance(t,k.PCB_ARC),'Arc tangent audit required before accepting MIPI arcs'
        a,b=t.GetStart(),t.GetEnd()
        for p,q in [(a,b),(b,a)]:
            key=(t.GetNetname(),t.GetLayer(),round(p.x/1000),round(p.y/1000))
            nodes[key].append((q.x-p.x,q.y-p.y))
    corners=[]
    for (net,layer,x,y),vectors in nodes.items():
        if len(vectors)!=2:continue # Connectivity/branch checks run separately.
        a,b=vectors
        cosine=(a[0]*b[0]+a[1]*b[1])/(math.hypot(*a)*math.hypot(*b))
        angle=math.degrees(math.acos(max(-1.,min(1.,cosine))))
        corners.append(dict(net=net,layer=board.GetLayerName(layer),
            board_relative_xy_mm=[x/1000-50,y/1000-50],interior_angle_degrees=angle))
    violations=[c for c in corners if c['interior_angle_degrees']<134.99]
    return dict(junctions_checked=len(corners),minimum_interior_angle_degrees=min(c['interior_angle_degrees'] for c in corners),
        acute_corners=sum(c['interior_angle_degrees']<89.99 for c in corners),
        maximum_heading_change_degrees=45,tolerance_degrees=.01,violations=violations)

def review(baseline,project,evidence):
    b=k.LoadBoard(str(project/'display_carrier.kicad_pcb'))
    old=k.LoadBoard(str(baseline/'display_carrier.kicad_pcb'))
    fps={f.GetReference():f for f in b.GetFootprints()};prior={f.GetReference():f for f in old.GetFootprints()}
    assert set(fps)==set(prior)
    def fid(f):return str(f.GetFPID().GetLibNickname())+':'+str(f.GetFPID().GetLibItemName())
    shrink=json.loads((project/'PASSIVE_SHRINK.json').read_text('utf8'));changed={r['reference'] for r in shrink['changes']}
    assert len(changed)==155
    for ref,f in fps.items():
        p=prior[ref];assert f.m_Uuid.AsString()==p.m_Uuid.AsString(),ref
        assert f.GetPosition()==p.GetPosition() and f.GetOrientationDegrees()==p.GetOrientationDegrees(),ref
        oldpads={z.m_Uuid.AsString():z for z in p.Pads()}
        for z in f.Pads():
            priorpad=oldpads[z.m_Uuid.AsString()]
            assert z.GetNetname()==priorpad.GetNetname() and z.GetNumber()==priorpad.GetNumber(),ref
        if ref in changed:assert fid(f).endswith('_0402_1005Metric')
        else:assert fid(f)==fid(p),ref
    # In every schematic, the only permitted edits are the selected Footprint
    # property strings. No electrical/geometry/UUID change may hide in a rewrite.
    for path in project.glob('*.kicad_sch'):
        a=sexp.load_file(str(baseline/path.name));c=sexp.load_file(str(path))
        for sym in sexp.find_all(c,'symbol'):
            props={str(n[1]):n for n in sexp.find_all(sym,'property')}
            ref=str(props.get('Reference',['','',''])[2])
            if ref in changed:props['Footprint'][2]=sexp.QStr.of(fid(prior[ref]))
        assert a==c,path.name
    def invariants(path):
        t=sexp.load_file(str(path))
        return [n for n in t if isinstance(n,list) and sexp.tag_of(n) in ['gr_line','gr_arc','gr_poly','gr_rect','gr_text']]
    assert invariants(baseline/'display_carrier.kicad_pcb')==invariants(project/'display_carrier.kicad_pcb')
    tracks=list(b.GetTracks());mipi=[t for t in tracks if t.GetNetname().startswith(('DSI_','CSI_'))]
    assert all(t.IsLocked() for t in mipi)
    corners=review_mipi_corners(b)
    assert not corners['violations'],corners
    planes={z.GetLayer():z.GetFilledPolysList(z.GetLayer()) for z in b.Zones() if not z.GetIsRuleArea()}
    assert set(planes)=={k.In1_Cu,k.In2_Cu}
    def pt(v):return (round(v.x/1000),round(v.y/1000)) # 1 um graph tolerance for native integer rounding
    def mmpt(v):return (k.ToMM(v.x),k.ToMM(v.y))
    rows={};missing=[];samples=0
    for net in sorted({t.GetNetname() for t in mipi}):
        tt=[t for t in mipi if t.GetNetname()==net];vias=[t for t in tt if isinstance(t,k.PCB_VIA)];traces=[t for t in tt if not isinstance(t,k.PCB_VIA)]
        adj=collections.defaultdict(set)
        def edge(a,c):adj[a].add(c);adj[c].add(a)
        for t in traces:
            assert t.GetLayer() in [k.F_Cu,k.B_Cu] and abs(k.ToMM(t.GetWidth())-.112)<1e-6
            edge((*pt(t.GetStart()),t.GetLayer()),(*pt(t.GetEnd()),t.GetLayer()))
            a,c=mmpt(t.GetStart()),mmpt(t.GetEnd());length=math.dist(a,c);steps=max(1,math.ceil(length/.05))
            ref_layer=k.In1_Cu if t.GetLayer()==k.F_Cu else k.In2_Cu
            for i in range(steps+1):
                p=(a[0]+(c[0]-a[0])*i/steps,a[1]+(c[1]-a[1])*i/steps)
                # Signal-via antipads necessarily interrupt the reference at a
                # transition; adjacent GND vias are verified separately below.
                if any(math.dist(p,mmpt(v.GetPosition()))<.6 for v in vias):continue
                samples+=1
                if not planes[ref_layer].Contains(k.VECTOR2I(k.FromMM(p[0]),k.FromMM(p[1]))):missing.append((net,p,ref_layer))
        for v in vias:edge((*pt(v.GetPosition()),k.F_Cu),(*pt(v.GetPosition()),k.B_Cu))
        pads=[p for f in fps.values() for p in f.Pads() if p.GetNetname()==net];assert len(pads)==2
        start=(*pt(pads[0].GetPosition()),k.F_Cu);end=(*pt(pads[1].GetPosition()),k.F_Cu)
        todo=[start];visited=set()
        while todo:
            x=todo.pop()
            if x in visited:continue
            visited.add(x);todo.extend(adj[x]-visited)
        assert end in visited and set(adj)==visited,(net,'disconnected copper')
        assert max(map(len,adj.values()))<=2,(net,'branch or stub')
        length=sum(k.ToMM(t.GetLength()) for t in traces)
        # Measure overlap with parallel opposite-polarity copper at the chosen
        # center spacing. Fanout and local phase compensation remain explicit.
        partner=net[:-1]+('N' if net.endswith('_P') else 'P')
        coupled=0.
        for t in traces:
            a,c=mmpt(t.GetStart()),mmpt(t.GetEnd());ln=math.dist(a,c)
            u=((c[0]-a[0])/ln,(c[1]-a[1])/ln);intervals=[]
            for q in mipi:
                if isinstance(q,k.PCB_VIA) or q.GetNetname()!=partner or q.GetLayer()!=t.GetLayer():continue
                x,y=mmpt(q.GetStart()),mmpt(q.GetEnd());dx,dy=y[0]-x[0],y[1]-x[1]
                if abs(dx*u[1]-dy*u[0])>1e-5:continue
                distance=abs((x[0]-a[0])*u[1]-(x[1]-a[1])*u[0])
                if abs(distance-.262)>.002:continue
                ts=sorted([(p[0]-a[0])*u[0]+(p[1]-a[1])*u[1] for p in [x,y]])
                lo=max(0,ts[0]);hi=min(ln,ts[1])
                if lo<hi:intervals.append((lo,hi))
            end=-1
            for lo,hi in sorted(intervals):
                coupled+=max(0,hi-max(end,lo));end=max(end,hi)
        rows[net]=dict(planar_length_mm=length,signal_vias=len(vias),barrel_inclusive_length_mm=length+len(vias)*1.5642,
                       outer_equivalent_delay_length_mm=length+len(vias)*1.5642*math.sqrt(4.42/2.93842064783),
                       coupled_at_0_15mm_gap_mm=coupled,fanout_and_corner_uncoupled_mm=length-coupled,connected=True)
    assert not missing,missing[:8]
    pairskews={n[:-2]:abs(v['planar_length_mm']-rows[n[:-1]+'N']['planar_length_mm']) for n,v in rows.items() if n.endswith('_P')}
    assert max(pairskews.values())<=.254
    bus_skews={}
    for prefix in ['DSI_','CSI_']:
        ll=[v['barrel_inclusive_length_mm'] for n,v in rows.items() if n.startswith(prefix)]
        delay=[v['outer_equivalent_delay_length_mm'] for n,v in rows.items() if n.startswith(prefix)]
        bus_skews[prefix]=dict(geometric_mm=max(ll)-min(ll),estimated_delay_equivalent_mm=max(delay)-min(delay))
        assert max(ll)-min(ll)<=.762
    returns=json.loads((project/'GROUND_ROUTING.json').read_text('utf8'))['return_vias']
    gv=[t for t in tracks if isinstance(t,k.PCB_VIA) and t.GetNetname()=='GND']
    for r in returns:
        for xy in r['ground_vias']:
            assert any(math.dist(mmpt(v.GetPosition()),(xy[0]+50,xy[1]+50))<.00001 for v in gv)
        assert len(r['ground_vias'])==2
        assert all(min(math.dist(s,g) for g in r['ground_vias'])<2.1 for s in r['signal_vias'])
    drc=json.loads((evidence/'drc-final.json').read_text('utf8'));assert not drc['violations'] and not drc['schematic_parity']
    rows_bom=list(csv.DictReader((project/'PROCUREMENT_100PCS.csv').read_text('utf-8-sig').splitlines()))
    assert all(int(r['ObservedStock'])>=3*int(r['ProposedQty']) for r in rows_bom if r['StockChannel']=='lcsc_domestic_direct' and int(r['ProposedQty']))
    cn=b.GetConnectivity();cn.Build(b)
    result=dict(revision='Q',status='PASS_PARTIAL_ROUTING_NOT_RELEASED',passives_changed=155,
        schematic_change='Only 155 Footprint properties; all electrical and drawing trees preserved',
        pad_and_footprint_UUIDs_preserved=True,all_component_centers_and_angles_preserved=True,
        board_outline_slots_and_drawings_preserved=True,planes='In1.Cu and In2.Cu GND; no inner signal tracks',
        mipi=rows,mipi_corners=corners,pair_skew_mm=pairskews,bus_skew=bus_skews,reference_plane_samples_checked=samples,
        reference_plane_gaps_outside_transition_antipads=0,paired_ground_returns=len(returns),
        total_tracks_and_vias=len(tracks),remaining_unconnected=cn.GetUnconnectedCount(False),
        physical_drc=0,schematic_parity=0,signal_integrity='Not simulated or measured; effective dielectric via delay is a first-order budget only',
        impedance='JLC public calculator estimate 98.738 ohm; production compensation and coupons require board-factory confirmation')
    (project/'ROUTING_REVIEW.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps({key:v for key,v in result.items() if key!='mipi'},indent=2))

if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__)
    for n in ['baseline','project','evidence']:a.add_argument('--'+n,type=Path,required=True)
    x=a.parse_args();review(x.baseline,x.project,x.evidence)
