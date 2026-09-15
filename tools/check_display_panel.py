"""Verify the selected display's actual SCH/PCB pin map with native KiCad.

Run with KiCad's Python (pcbnew required). This checks CAD correspondence;
unknown panel ratings are reported separately, never inferred from ERC/DRC.
"""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
import pcbnew as k


def check(project: Path, output: Path, cli: str):
    output.mkdir(parents=True, exist_ok=True)
    profile = json.loads((project / 'panel-profile.json').read_text(encoding='utf-8'))
    netlist = output / 'display-panel.net.xml'
    subprocess.run([cli, 'sch', 'export', 'netlist', '--format', 'kicadxml', '--output',
                    str(netlist), str(project / 'display_singleboard.kicad_sch')], check=True)
    schematic_nets = {}
    for net in ET.parse(netlist).getroot().findall('./nets/net'):
        for node in net.findall('node'):
            schematic_nets[(node.attrib['ref'], node.attrib['pin'])] = net.attrib['name']
    board = k.LoadBoard(str(project / 'display_singleboard.kicad_pcb'))
    footprints = {f.GetReference(): f for f in board.GetFootprints()}
    errors, pins = [], []
    def normalized(net):
        return None if not net or net.startswith('unconnected-(') else net
    for connector in ('lcd_connector', 'touch_connector'):
        spec = profile[connector]
        ref = spec['reference']
        fp = footprints.get(ref)
        if fp is None:
            errors.append(f'{ref}: footprint missing')
            continue
        pads = {p.GetNumber(): p for p in fp.Pads()}
        expected_layer = k.B_Cu if spec['board_side'] == 'B' else k.F_Cu
        if fp.GetLayer() != expected_layer:
            errors.append(f'{ref}: wrong board side')
        for number, expected in spec['pin_nets'].items():
            sch = normalized(schematic_nets.get((ref, number)))
            pcb = normalized(pads[number].GetNetname()) if number in pads else 'PAD MISSING'
            row = {'reference': ref, 'pin': number, 'expected': expected, 'schematic': sch, 'pcb': pcb}
            pins.append(row)
            if sch != expected or pcb != expected:
                errors.append(row)
    dsi = {}
    for pair in ('CLK', 'DATA0', 'DATA1'):
        values = {}
        for polarity in 'PN':
            name = 'DSI_A_' + pair + polarity
            items = [t for t in board.GetTracks() if t.GetNetname() == name]
            vias = [t for t in items if isinstance(t, k.PCB_VIA)]
            values[polarity] = {'planar_length_mm': round(sum(k.ToMM(t.GetLength()) for t in items if not isinstance(t, k.PCB_VIA)), 6),
                                'via_count': len(vias)}
        values['planar_skew_mm'] = round(abs(values['P']['planar_length_mm'] - values['N']['planar_length_mm']), 6)
        dsi[pair] = values
    report = {'pin_map_passed': not errors, 'pins_checked_including_mounting_lands': len(pins),
              'errors': errors, 'pins': pins, 'dsi_connector_side_copper': dsi,
              'panel_qualification': profile['qualification'],
              'note': 'Copper lengths exclude MCU-side series-resistor nets and internal package/FPC lengths. Connectivity and clearance require a separate native DRC.'}
    (output / 'panel-check.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'pins'}, ensure_ascii=False, indent=2))
    return not errors


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, default=Path('display_board/hardware/display-singleboard'))
    parser.add_argument('--output', type=Path, default=Path('build/display-panel-check'))
    parser.add_argument('--cli', default=shutil.which('kicad-cli') or str(Path(sys.executable).with_name('kicad-cli.exe')))
    args = parser.parse_args()
    raise SystemExit(0 if check(args.project.resolve(), args.output.resolve(), args.cli) else 1)
