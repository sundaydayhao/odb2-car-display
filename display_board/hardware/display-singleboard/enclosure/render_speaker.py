"""Render a true section of the generated acoustic subassembly, not a concept image."""
from pathlib import Path
import cadquery as cq
import vtk,json,os,sys
P=Path(__file__).parent;M=P/'models'
clip=cq.Workplane('XY').box(65,22,40,centered=(True,True,False)).translate((29,-22,0)).val()
items=[('rear_service_cover',(.55,.62,.70)),('speaker_baffle_duct',(.22,.48,.70)),('speaker_volume_insert',(.70,.60,.38)),('reference-wired_speaker_ASSUMED',(.15,.18,.20))]
ren=vtk.vtkRenderer();ren.SetBackground(.95,.97,.98)
for name,color in items:
    sh=cq.importers.importStep(str(M/(name+'.step'))).val().intersect(clip)
    path=M/('_audio_'+name+'.stl');cq.exporters.export(sh,str(path),tolerance=.05,angularTolerance=.15)
    r=vtk.vtkSTLReader();r.SetFileName(str(path));r.Update();m=vtk.vtkPolyDataMapper();m.SetInputConnection(r.GetOutputPort());a=vtk.vtkActor();a.SetMapper(m);a.GetProperty().SetColor(*color);a.GetProperty().SetAmbient(.3);a.GetProperty().SetDiffuse(.7);ren.AddActor(a)
camera=ren.GetActiveCamera();camera.SetPosition(-25,95,85);camera.SetFocalPoint(26,-17,22);camera.SetViewUp(0,0,1);camera.ParallelProjectionOn();camera.SetParallelScale(33)
for text,x,y,size in [('A7 / SEALED SPEAKER SECTION',25,900,27),('C3012621 prototype candidate | 40 x 20 x 8 mm conservative body',25,64,18),('Left duct toward driver | nominal net rear air 5.032 cc | rear cover has no sound grille',25,39,17),('Supplier height / temperature conflict and in-car acoustic performance remain unqualified.',25,14,17)]:
    a=vtk.vtkTextActor();a.SetInput(text);a.SetPosition(x,y);a.GetTextProperty().SetFontSize(size);a.GetTextProperty().SetColor(.1,.15,.2);ren.AddActor2D(a)
win=vtk.vtkRenderWindow();win.SetOffScreenRendering(1);win.SetSize(1500,950);win.AddRenderer(ren);win.SetMultiSamples(4);win.Render();capture=vtk.vtkWindowToImageFilter();capture.SetInput(win);capture.ReadFrontBufferOff();capture.Update();wr=vtk.vtkPNGWriter();wr.SetFileName(str(M/'speaker-section.png'));wr.SetInputConnection(capture.GetOutputPort());wr.Write();win.Finalize();sys.stdout.flush();os._exit(0)
