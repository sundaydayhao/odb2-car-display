"""Revision P footprints, with lands derived from the cited package drawings.

Run with native KiCad Python. Simplified VRML models are dimension references,
not manufacturer mating models; all switch enclosure fits remain unqualified.
"""
import argparse, hashlib, json, shutil
from pathlib import Path
import pcbnew as k

LIBS=Path('C:/Program Files/KiCad/10.0/share/kicad')
def v(x,y):return k.VECTOR2I(k.FromMM(x),k.FromMM(y))
def ls(*layers):
    result=k.LSET()
    for layer in layers:result.AddLayer(layer)
    return result


def generate(project):
    fpdir=project/'Carrier.pretty';md=project/'Carrier.3dshapes'
    manifest=json.loads((project/'model-manifest.json').read_text(encoding='utf-8'))
    plugin=k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP)
    def rect(f,x1,y1,x2,y2,layer=k.F_Fab,width=.1):
        g=k.PCB_SHAPE(f);g.SetShape(k.SHAPE_T_RECT);g.SetStart(v(x1,y1));g.SetEnd(v(x2,y2))
        g.SetLayer(layer);g.SetWidth(k.FromMM(width));f.Add(g)
    def blank(name,w,h,margin=.25):
        f=k.FOOTPRINT(None);f.SetFPID(k.LIB_ID('Carrier',name));f.SetAttributes(k.FP_SMD)
        rect(f,-w/2,-h/2,w/2,h/2)
        rect(f,-w/2-margin,-h/2-margin,w/2+margin,h/2+margin,k.F_CrtYd,.05)
        return f
    def pad(f,n,x,y,w,h,drill=0,slot=None,npth=False,paste=True):
        p=k.PAD(f);p.SetNumber(str(n));p.SetPosition(v(x,y));p.SetSize(v(w,h))
        p.SetShape(k.PAD_SHAPE_OVAL if drill or npth else k.PAD_SHAPE_RECT)
        p.SetAttribute(k.PAD_ATTRIB_NPTH if npth else k.PAD_ATTRIB_PTH if drill else k.PAD_ATTRIB_SMD)
        if drill or npth:
            layers=k.LSET.AllCuMask();layers.AddLayer(k.F_Mask);layers.AddLayer(k.B_Mask);p.SetLayerSet(layers)
            p.SetDrillSize(v(*(slot or (drill,drill))))
            if slot:p.SetDrillShape(k.PAD_DRILL_SHAPE_OBLONG)
        else:
            layers=ls(k.F_Cu,k.F_Mask)
            if paste:layers.AddLayer(k.F_Paste)
            p.SetLayerSet(layers)
        f.Add(p);return p
    def save(f,name,refs,source,dimensions,old='',boxes=None):
        f.SetFPID(k.LIB_ID('Carrier',name));f.SetValue(name)
        f.Reference().SetVisible(False);f.Value().SetVisible(False)
        # Millimetre coordinate envelope, converted to VRML's 0.1-inch units.
        boxes=boxes or [(0,0,dimensions[2]/2,*dimensions)]
        model=md/(name+'.wrl')
        body=['#VRML V2.0 utf8','# Datasheet envelope only; see SOURCING_REVIEW.md',
            'Transform { scale 0.3937007874 0.3937007874 0.3937007874 children [']
        for x,y,z,w,h,t in boxes:
            body.append(f'Transform {{ translation {x} {y} {z} children [ Shape {{ appearance Appearance {{ material Material {{ diffuseColor 0.18 0.19 0.20 }} }} geometry Box {{ size {w} {h} {t} }} }} ] }}')
        body.append('] }');model.write_text('\n'.join(body)+'\n',encoding='utf-8')
        f.Models().clear();m=k.FP_3DMODEL();m.m_Filename='${KIPRJMOD}/Carrier.3dshapes/'+model.name;f.Add3DModel(m)
        plugin.FootprintSave(str(fpdir.resolve()),f)
        manifest['models']=[r for r in manifest['models'] if r['reference'] not in refs]
        for ref in refs:
            manifest['models'].append(dict(reference=ref,original_footprint=old or 'Carrier:'+name,
                footprint='Carrier:'+name,file=model.name,offset_mm=[0,0,0],rotation_deg=[0,0,0],scale=[1,1,1],
                kind='datasheet_reference',source=source,note='Dimension envelope; not an assembly fit qualification.',
                sha256=hashlib.sha256(model.read_bytes()).hexdigest(),body_mm=list(dimensions)))
    def standard(lib,name,new,refs,source,dim):
        f=k.FootprintLoad(str(LIBS/'footprints'/(lib+'.pretty')),name);assert f,(lib,name)
        save(f,new,refs,source,dim)

    # ES8388 datasheet rev5 pp5,35: pin1 upper-left, counterclockwise top view.
    f=blank('ES8388_QFN28_4x4_P0.45',4,4,.6)
    for i in range(7):
        pad(f,i+1,-1.975,-1.35+i*.45,.65,.23)
        pad(f,i+8,-1.35+i*.45,1.975,.23,.65)
        pad(f,i+15,1.975,1.35-i*.45,.65,.23)
        pad(f,i+22,1.35-i*.45,-1.975,.23,.65)
    pad(f,29,0,0,2.6,2.6,paste=False)
    for x in [-.65,.65]:
        for y in [-.65,.65]:
            z=pad(f,'',x,y,1.05,1.05);z.SetLayerSet(ls(k.F_Paste))
    rect(f,-1.95,-1.95,-1.7,-1.7,k.F_Fab,.1)
    save(f,'ES8388_QFN28_4x4_P0.45',['U800'],'ES8388 rev5 pp5,35; LCSC C365736',(4,4,.9))
    standard('Package_SO','SSOP-24_3.9x8.7mm_P0.635mm','FSV4730_SSOP24',['U815'],
             'FSV4730 rev1.1 pp31,34; LCSC C19191931',(3.9,8.7,1.75))
    standard('Package_TO_SOT_SMD','SOT-23','SOT23_MCP120',['U816'],
             'Microchip DS11184D; pin1 RESET, 2 VDD, 3 VSS',(2.9,1.3,1.1))
    standard('Diode_SMD','D_SOD-123F','BAT54H_SOD123F',['D211','D212','D310','D311'],
             'Nexperia BAT54H SOD123F; pin1 cathode; C426769',(2.6,1.6,1.1))
    standard('Capacitor_Tantalum_SMD','CP_EIA-7343-31_Kemet-D','T520D_7343_31',['C1012','C1013'],
             'KEMET T520D227M010ATE025 D case; C116743',(7.3,4.3,3.1))
    # Sensirion membrane option retains DFN4 lands; only the model height changes.
    old=next(r for r in manifest['models'] if r['reference']=='U1301')
    f=k.FootprintLoad(str(fpdir.resolve()),old['footprint'].split(':')[1])
    save(f,'SHT40_AD1F_DFN4_Membrane',['U1301'],'Sensirion SHT4x v7.3; C7461846',(1.5,1.5,.69))
    for name,refs,w,h,px,pw,ph,source in [
        ('L_DMBJ_CXP0520',['L300'],5.2,5.7,1.8,1.6,2.5,'CXP0520 drawing pp3,4; C7461343'),
        ('L_Sunlord_MWSA0402S',['L410','L1010','L820'],4.4,4.2,1.85,1.5,2.5,'Sunlord MWSA0402S 2023 pp2,3; C408332/C408335')]:
        f=blank(name,w,h,.55);pad(f,1,-px,0,pw,ph);pad(f,2,px,0,pw,ph)
        save(f,name,refs,source,(w,h,2.0))
    f=blank('SXTF3215R_32k',3.2,1.5,.45)
    pad(f,1,-1.25,0,1.0,1.8);pad(f,2,1.25,0,1.0,1.8)
    save(f,'SXTF3215R_32k',['Y810'],'SXTF3215R32768A20F package drawing; C22386778',(3.2,1.5,.9))

    # Manufacturer mounting pattern: signal holes 4.5 mm pitch, shell holes
    # 6.8 mm pitch and a 2.5 x 0.6 mm central non-plated locating slot.
    f=k.FOOTPRINT(None);f.SetAttributes(k.FP_THROUGH_HOLE)
    rect(f,-4.2,-4.2,4.2,2.3);rect(f,-4.7,-4.7,4.7,5.35,k.F_CrtYd,.05)
    pad(f,1,-2.25,0,1.9,1.9,1.1);pad(f,2,2.25,0,1.9,1.9,1.1)
    for x in [-3.4,3.4]:pad(f,'SH',x,-2.5,2.1,2.1,1.4)
    pad(f,'',0,2.3,2.5,.6,slot=(2.5,.6),npth=True)
    # Actuator projection in Fab only, outside the circuit-board boundary.
    rect(f,-1.65,2.3,1.65,4.85)
    save(f,'TS038_SideKey',['SW101','SW102'],'Zhongdi TS-038-060250-D2 manufacturer drawing; C52178082',
         (8.4,6.5,7.2),boxes=[(0,.95,3.6,8.4,6.5,7.2),(0,-3.575,3.6,3.3,2.55,3.3)])
    f=blank('SK22D02L4_SideSlide',10.8,6.1,.8);f.SetAttributes(k.FP_THROUGH_HOLE)
    for n in range(6):pad(f,n+1,2*(n%3-1),-1.25 if n<3 else 1.25,1.4,1.4,.8)
    for x in [-5.2,5.2]:pad(f,'SH',x,0,1.4,2.2,.7,slot=(.7,1.5))
    rect(f,-3,3.05,0,7.05)
    save(f,'SK22D02L4_SideSlide',['SW310'],'XKB SK22D02 L=4 drawing; C609825',
         (10.8,6.1,5.2),boxes=[(0,0,2.6,10.8,6.1,5.2),(-1.5,-5.05,2.6,3,4,2)])
    manifest['scope']='Revision P: every new package bound to a local datasheet dimension envelope; fit not validated.'
    (project/'model-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--project',type=Path,required=True)
    generate(ap.parse_args().project.resolve())
