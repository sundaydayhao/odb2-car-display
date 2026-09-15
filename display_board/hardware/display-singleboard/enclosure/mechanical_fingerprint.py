"""Hash mechanical placement/pads/3D references while excluding copper and silk."""
from pathlib import Path
import argparse,hashlib,json
import pcbnew
ap=argparse.ArgumentParser();ap.add_argument('--pcb',type=Path,required=True);ap.add_argument('--model-project',type=Path);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--compare',type=Path);a=ap.parse_args()
b=pcbnew.LoadBoard(str(a.pcb));root=a.model_project or a.pcb.parent
def pt(v):return [round(pcbnew.ToMM(v.x),5),round(pcbnew.ToMM(v.y),5)]
def vec(v):return [round(v.x,6),round(v.y,6),round(v.z,6)]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def model(m):
 path=Path(m.m_Filename.replace('${KIPRJMOD}',str(root.resolve())))
 return {'path':m.m_Filename,'sha256':sha(path) if path.exists() else None,'offset':vec(m.m_Offset),'scale':vec(m.m_Scale),'rotation':vec(m.m_Rotation),'show':m.m_Show}
fp={}
for f in sorted(b.GetFootprints(),key=lambda f:f.GetReference()):
 pads=[]
 for pad in f.Pads():
  pads.append({'number':pad.GetNumber(),'xy':pt(pad.GetPosition()),'size':pt(pad.GetSize()),'drill':pt(pad.GetDrillSize()),'shape':int(pad.GetShape()),'angle':round(pad.GetOrientationDegrees(),5),'layers':pad.GetLayerSet().FmtHex(),'attribute':int(pad.GetAttribute())})
 libid=f.GetFPID();fp[f.GetReference()]={'footprint':str(libid.GetLibNickname())+':'+str(libid.GetLibItemName()),'xy':pt(f.GetPosition()),'angle':round(f.GetOrientationDegrees(),5),'layer':f.GetLayerName(),'dnp':f.IsDNP(),'models':[model(m) for m in f.Models()],'pads':sorted(pads,key=lambda p:json.dumps(p,sort_keys=True))}
edges=[]
for d in b.GetDrawings():
 if d.GetLayer()==pcbnew.Edge_Cuts:
  edges.append({'shape':int(d.GetShape()),'start':pt(d.GetStart()),'end':pt(d.GetEnd()),'center':pt(d.GetCenter()),'width':round(pcbnew.ToMM(d.GetWidth()),5)})
geometry={'footprints':fp,'edge_cuts':sorted(edges,key=lambda p:json.dumps(p,sort_keys=True)),'nominal_thickness_mm':round(pcbnew.ToMM(b.GetDesignSettings().GetBoardThickness()),5)}
report={'source_pcb_sha256':sha(a.pcb),'source_path':str(a.pcb),'geometry_sha256':hashlib.sha256(json.dumps(geometry,sort_keys=True).encode()).hexdigest(),'scope':'All footprint identity/pose/pad geometry and model content/transform; Edge.Cuts; ignores copper, vias, zones, nets, UUIDs and silk','geometry':geometry}
if a.compare:
 prev=json.loads(a.compare.read_text());report['comparison']={'reference_geometry_sha256':prev['geometry_sha256'],'same_geometry':report['geometry_sha256']==prev['geometry_sha256'],'changed_refs':[r for r in sorted(set(fp)|set(prev['geometry']['footprints'])) if fp.get(r)!=prev['geometry']['footprints'].get(r)],'outline_changed':geometry['edge_cuts']!=prev['geometry']['edge_cuts']}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps({k:v for k,v in report.items() if k!='geometry'},ensure_ascii=False))
