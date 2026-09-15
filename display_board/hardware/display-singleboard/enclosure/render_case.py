"""Render STL inspection evidence using the installed local VTK runtime."""
import argparse,json,os,sys
from pathlib import Path
import vtk

def render(root,mode):
    manifest=root/'render-parts-real.json'
    entries=json.loads((manifest if manifest.exists() else root/'render-parts.json').read_text())
    ren=vtk.vtkRenderer();ren.SetBackground(.93,.95,.97)
    for q in entries:
        name=q['name']
        if mode=='rear_open' and (name=='rear_service_cover' or name.startswith('speaker_') or name=='wired_speaker_ASSUMED'):continue
        reader=vtk.vtkSTLReader();reader.SetFileName(q['path']);reader.Update()
        mapper=vtk.vtkPolyDataMapper();mapper.SetInputConnection(reader.GetOutputPort())
        actor=vtk.vtkActor();actor.SetMapper(mapper);actor.GetProperty().SetColor(*q['color']);actor.GetProperty().SetInterpolationToPhong()
        actor.GetProperty().SetSpecular(.25);actor.GetProperty().SetSpecularPower(30)
        actor.GetProperty().SetAmbient(.3);actor.GetProperty().SetDiffuse(.7)
        if mode=='exploded':
            dz=35 if name=='rear_service_cover' or name.startswith('speaker_') or name=='wired_speaker_ASSUMED' else (12 if name=='PCB_envelope_CANDIDATE' or name.startswith('PCB_REAL_') else (-20 if name in ['front_frame','LCD_envelope_ASSUMED','wired_microphone_ASSUMED'] or name.startswith('panel_edge_foam') else 0))
            actor.SetPosition(0,0,dz)
        ren.AddActor(actor)
    camera=ren.GetActiveCamera();cx,cy,cz=29,29.5,18
    if mode=='front':camera.SetPosition(cx-180,cy+180,-250)
    elif mode=='exploded':camera.SetPosition(cx+210,cy+240,cz+270)
    elif mode=='rear_open':camera.SetPosition(cx+150,cy+190,cz+290)
    elif mode=='left':camera.SetPosition(cx-260,cy+70,cz+120)
    elif mode=='right':camera.SetPosition(cx+270,cy+40,cz+110)
    elif mode=='top':camera.SetPosition(cx,cy+340,cz+90)
    else:camera.SetPosition(cx+200,cy+160,cz+250)
    camera.SetFocalPoint(cx,cy,cz);camera.SetViewUp(0,1,0);camera.ParallelProjectionOn();camera.SetParallelScale(109 if mode!='exploded' else 120)
    title=vtk.vtkTextActor();title.SetInput({'front':'A7 / FRONT FRAME','rear':'A7 / SERVICE COVER','rear_open':'A7 / OPEN REAR ACCESS','exploded':'A7 / ASSEMBLY EXPLODED','left':'A7 / LEFT: POWER IN + USB DEBUG + SOUND','right':'A7 / RIGHT: microSD ONLY','top':'A7 / TOP: POWER SLIDE + SERVICE HOLES'}[mode]);title.SetPosition(35,1045);title.GetTextProperty().SetFontSize(28);title.GetTextProperty().SetColor(.1,.15,.2);ren.AddActor2D(title)
    footer=vtk.vtkTextActor();footer.SetInput('PARAMETRIC FIT PROTOTYPE  |  178 x 126 x 37.2 mm\nPanel glass, flex length, port mating and audio parts need measured confirmation.');footer.SetPosition(35,25);footer.GetTextProperty().SetFontSize(17);footer.GetTextProperty().SetColor(.25,.3,.35);ren.AddActor2D(footer)
    win=vtk.vtkRenderWindow();win.SetOffScreenRendering(1);win.SetSize(1600,1100);win.AddRenderer(ren);win.SetMultiSamples(4);win.Render()
    capture=vtk.vtkWindowToImageFilter();capture.SetInput(win);capture.ReadFrontBufferOff();capture.Update()
    writer=vtk.vtkPNGWriter();writer.SetFileName(str(root/f'{mode}.png'));writer.SetInputConnection(capture.GetOutputPort());writer.Write();win.Finalize();print(mode,flush=True)

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--models',type=Path,default=Path(__file__).parent/'models');a=ap.parse_args()
    for mode in ['front','rear','rear_open','exploded','left','right','top']:render(a.models,mode)
    sys.stdout.flush();os._exit(0)
