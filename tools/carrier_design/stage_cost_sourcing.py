"""Stage the small cost/lead-time delta without regenerating routed geometry."""
import copy,json,shutil,hashlib,argparse
from pathlib import Path
import pcbnew as k
from cad import sexp
from cost_sourcing_spec import CHANGES,apply,POLYMER_PDF

Q=sexp.QStr.of
def props(n):return {str(x[1]):x for x in sexp.find_all(n,'property')}

def stage(baseline,output):
 assert baseline.resolve()!=output.resolve()
 shutil.copytree(baseline,output,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.history','*.xls','*.xlsx','*.kicad_prl','~*'))
 intent=json.loads((output/'design-intent.json').read_text('utf8'));oldintent=copy.deepcopy(intent)
 apply(intent['parts']);parts={p['ref']:p for p in intent['parts']}
 (output/'design-intent.json').write_text(json.dumps(intent,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 name='KNSCHA_177EC0013_7343';fpdir=output/'Carrier.pretty'
 t=sexp.load_file(str(fpdir/'T520D_7343_31.kicad_mod'));t[1]=Q(name)
 props(t)['Value'][2]=Q('177EC0013')
 desc=sexp.find(t,'descr')
 if desc:desc[1]=Q('KNSCHA 177EC0013; manufacturer pp1,3; 8.6mm outer / 3.8mm gap / 2.8mm pad height')
 def cap_geometry(t):
  for p in sexp.find_all(t,'pad'):
   a=sexp.find(p,'at');a[1]=-3.1 if float(a[1])<0 else 3.1
   sexp.find(p,'size')[1:]=[2.4,2.8]
  for g in sexp.find_all(t,'fp_line'):
   layer=sexp.find(g,'layer')
   if layer and str(layer[1])=='F.CrtYd':
    for key in ['start','end']:
     a=sexp.find(g,key)
     a[1]=(1 if float(a[1])>0 else -1)*4.55
     a[2]=(1 if float(a[2])>0 else -1)*2.55
  for g in sexp.find_all(t,'fp_rect'):
   if str(sexp.find(g,'layer')[1])=='F.CrtYd':
    sexp.find(g,'start')[1:]=[-4.55,-2.55];sexp.find(g,'end')[1:]=[4.55,2.55]
  sexp.find(t,'model')[1]=Q('${KIPRJMOD}/Carrier.3dshapes/'+name+'.wrl')
 cap_geometry(t);sexp.save_file(str(fpdir/(name+'.kicad_mod')),t)
 model=output/'Carrier.3dshapes'/(name+'.wrl')
 model.write_text('#VRML V2.0 utf8\n# KNSCHA maximum body envelope, not a mating model\nTransform { scale 0.3937007874 0.3937007874 0.3937007874 children [\nTransform { translation 0 0 1.1 children [ Shape { appearance Appearance { material Material { diffuseColor 0.18 0.19 0.20 } } geometry Box { size 7.6 4.6 2.2 } } ] }\n] }\n',encoding='utf8')
 manifest=json.loads((output/'model-manifest.json').read_text('utf8'))
 for row in manifest['models']:
  ref=row['reference']
  if ref in ['C1012','C1013','U1301']:
   file=name+'.wrl' if ref!='U1301' else 'SHT40_DFN4_reference.wrl'
   row.update(footprint=parts[ref]['footprint'],file=file,sha256=hashlib.sha256((output/'Carrier.3dshapes'/file).read_bytes()).hexdigest(),
       body_mm=[7.6,4.6,2.2] if ref!='U1301' else [1.5,1.5,.6],source=parts[ref]['source'],note='Cost revision R; maximum/reference package envelope; assembly fit remains unqualified')
 manifest['scope']='Revision R purchasing changes; local package models follow selected parts.'
 (output/'model-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 for path in output.glob('*.kicad_sch'):
  tree=sexp.load_file(str(path));changed=False
  if path.name=='17_cabin_sensors.kicad_sch':
   for note in sexp.find_all(tree,'text'):
    if 'SHT40-AD1F-R2' in str(note[1]):
     note[1]=Q(str(note[1]).replace('SHT40-AD1F-R2','SHT40-AD1B-R2'));changed=True
  for sym in sexp.find_all(tree,'symbol'):
   pr=props(sym);ref=str(pr.get('Reference',['','',''])[2])
   if ref not in CHANGES:continue
   for field,key in [('Value','value'),('Footprint','footprint'),('Datasheet','source'),('DesignStatus','status')]:
    pr[field][2]=Q(parts[ref][key])
   changed=True
  if changed:sexp.save_file(str(path),tree)
 pcb=output/'display_carrier.kicad_pcb';tree=sexp.load_file(str(pcb))
 for fp in sexp.find_all(tree,'footprint'):
  pr=props(fp);ref=str(pr['Reference'][2])
  if ref not in CHANGES:continue
  p=parts[ref];fp[1]=Q(p['footprint'])
  for field,key in [('Value','value'),('Datasheet','source'),('DesignStatus','status')]:pr[field][2]=Q(p[key])
  if ref in ['C1012','C1013']:cap_geometry(fp)
  if ref=='U1301':sexp.find(fp,'model')[1]=Q('${KIPRJMOD}/Carrier.3dshapes/SHT40_DFN4_reference.wrl')
 sexp.save_file(str(pcb),tree)
 board=k.LoadBoard(str(pcb));k.SaveBoard(str(pcb),board)
 changes={r:dict(before=next(p for p in oldintent['parts'] if p['ref']==r),after=parts[r],lcsc=CHANGES[r]['lcsc']) for r in CHANGES}
 (output/'COST_CHANGES.json').write_text(json.dumps(dict(revision='R',date='2026-09-25',changes=changes),ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print('Staged',len(changes),'references; tracks/vias',len(board.GetTracks()))

if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--baseline',type=Path,required=True);a.add_argument('--output',type=Path,required=True);x=a.parse_args();stage(x.baseline,x.output)
