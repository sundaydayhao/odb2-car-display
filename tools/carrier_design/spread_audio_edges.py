"""Move the two microphones and two speaker connectors to opposite board edges.

Operate only on the unrouted revision-P scratch project. Microphone signal
coupling remains near the codec; local supply filtering follows each microphone.
Native DRC and independent verification are required after applying this edit.
"""
import argparse,json
from pathlib import Path
import pcbnew as k

POSITIONS={
    'U802':(138,73),'U803':(7,73),
    'R840':(141.5,70),'C840':(135,76.25),'C848':(139,76.5),
    'R841':(3.25,69.5),'C844':(3.25,74),'C849':(3.25,71.75),
    'J820':(137.5,45.5),'J821':(6.5,44),
}

def apply(project):
    b=k.LoadBoard(str(project/'display_carrier.kicad_pcb'));assert not len(b.GetTracks())
    fps={f.GetReference():f for f in b.GetFootprints()};moves={}
    intent=json.loads((project/'design-intent.json').read_text('utf-8'))
    parts={p['ref']:p for p in intent['parts']}
    for ref,(x,y) in POSITIONS.items():
        f=fps[ref];assert f.GetLayer()==k.F_Cu
        moves[ref]={'before_xy_mm':[k.ToMM(f.GetPosition().x)-50,k.ToMM(f.GetPosition().y)-50],
                    'xy_mm':[x,y],'side':'F','angle':f.GetOrientationDegrees()}
        f.SetPosition(k.VECTOR2I(k.FromMM(x+50),k.FromMM(y+50)))
        parts[ref]['pcb']=[x,y]
    k.SaveBoard(str(project/'display_carrier.kicad_pcb'),b)
    (project/'design-intent.json').write_text(json.dumps(intent,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    sheet=project/'18_dual_mic.kicad_sch'
    sheet.write_text(sheet.read_text('utf-8').replace('60mm nominal center spacing','131mm nominal center spacing'),encoding='utf-8')
    report=dict(revision='P',reason='User requested maximum left/right microphone separation and one speaker on each side.',
        coordinate_view='F face / rear of screen; electrical L is at F-right and R is at F-left.',
        microphone_center_spacing_mm=131,positions=moves,
        speaker_note='These are the two external speaker connectors. Speaker bodies and acoustic chambers are enclosure parts, not PCB footprints.',
        routing='UNROUTED; route long analog microphone signals with quiet return paths and keep away from power switch nodes',
        acoustic_and_AEC_validation='NOT_RUN; update array geometry and calibrate after assembly')
    (project/'AUDIO_EDGE_LAYOUT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--project',type=Path,required=True)
    apply(ap.parse_args().project)
