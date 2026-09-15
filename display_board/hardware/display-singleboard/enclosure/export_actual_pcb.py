"""Export one immutable KiCad snapshot and all enclosure-critical body STEP files."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ap=argparse.ArgumentParser();ap.add_argument('--project',type=Path,required=True);ap.add_argument('--output',type=Path,default=Path(__file__).parent/'models');ap.add_argument('--kicad-cli',default='C:/Program Files/KiCad/10.0/bin/kicad-cli.exe');a=ap.parse_args()
pcb=a.project/'display_singleboard.kicad_pcb';before=hashlib.sha256(pcb.read_bytes()).hexdigest();a.output.mkdir(parents=True,exist_ok=True)
refs=['J19','J21','J22','SW3','SW1','SW2','J1','J2','J302','J308','J9','J7','J16','U1']
for ref in ['pcb','bare-pcb']+refs:
    args=[a.kicad_cli,'pcb','export','step','--no-dnp','--force','--user-origin','0x0mm','--output',str(a.output/f'actual-{ref}.step')]
    if ref=='bare-pcb':args+=['--board-only']
    elif ref!='pcb':args+=['--no-board-body','--component-filter',ref]
    result=subprocess.run(args+[str(pcb)],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    print(ref,flush=True)
after=hashlib.sha256(pcb.read_bytes()).hexdigest();assert before==after,'PCB changed during export; repeat when its geometry is stable'
(a.output/'actual-export-provenance.json').write_text(json.dumps({'pcb_path':str(pcb.resolve()),'pcb_sha256':after,'kicad_cli':a.kicad_cli,'refs':refs,'outputs_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [a.output/f'actual-{ref}.step' for ref in ['pcb','bare-pcb']+refs]}},indent=2),encoding='utf8')
