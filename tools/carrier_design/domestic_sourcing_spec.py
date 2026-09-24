"""Revision P: package-specific, domestic-stock substitutions (2026-09-24).

Apply after the earlier circuit revisions. This does not place existing parts;
stage_domestic_sourcing.py preserves the reviewed board and updates only changed
packages and the documented local placement adjustments.
"""

ES = 'https://atta.szlcsc.com/upload/public/pdf/source/20190124/C365736_5E84B6C58F11FDCF085DB830A572A0FC.pdf'
FM = 'https://atta.szlcsc.com/upload/public/pdf/source/20231110/3FE2255F8E542E1F23F397419E3992D5.pdf'
SHT = 'https://sensirion.com/media/documents/33FD6951/6A7C10A0/HT_DS_Datasheet_SHT4x_V7.3.pdf'


def apply(d):
    p = {p['ref']: p for p in d.parts}
    def change(ref, **kw): p[ref].update(kw)
    def box(name, pins):
        d.box_symbol(name, [(str(i), n, t) for i, n, t in pins])
    def add(sheet, ref, kind, value, nets, xy, pcb, footprint, source=''):
        d.add(sheet, ref, kind, value, nets, xy, footprint, pcb, side='F', source=source)
    def cap(ref, value, a, b, xy, pcb, sheet='12_fm_audio'):
        add(sheet, ref, 'C', value, {1:a, 2:b}, xy, pcb, 'Capacitor_SMD:C_0603_1608Metric')

    # ES8388 has separate differential-input selectors for the two ADCs.
    # LOUT1/ROUT1 are single-ended: the PA negative legs keep their AC ground
    # returns and mid-rail bias; LOUT2/ROUT2 must NOT be treated as inverted legs.
    box('ES8388', [(1,'MCLK','input'),(2,'DVDD','power_in'),(3,'PVDD','power_in'),
        (4,'DGND','power_in'),(5,'SCLK','input'),(6,'DSDIN','input'),(7,'LRCK','input'),
        (8,'ASDOUT','output'),(9,'NC1','no_connect'),(10,'VREF','passive'),
        (11,'ROUT1','output'),(12,'LOUT1','output'),(13,'HPGND','power_in'),
        (14,'ROUT2','output'),(15,'LOUT2','output'),(16,'HPVDD','power_in'),
        (17,'AVDD','power_in'),(18,'AGND','power_in'),(19,'ADCVREF','passive'),
        (20,'VMID','passive'),(21,'RIN2','input'),(22,'LIN2','input'),
        (23,'RIN1','input'),(24,'LIN1','input'),(25,'NC2','no_connect'),
        (26,'CE','input'),(27,'CDATA','bidirectional'),(28,'CCLK','input'),(29,'EP','power_in')])
    change('U800',kind='ES8388',value='ES8388',footprint='Carrier:ES8388_QFN28_4x4_P0.45',source=ES,
        status='I2S slave; 7-bit 0x10; ADC differential pairs 1 and 2; firmware port required',
        nets={str(k):v for k,v in {1:'CODEC_MCLK',2:'AUDIO_3V3',3:'AUDIO_3V3',4:'GND',
        5:'CODEC_BCLK',6:'CODEC_TDMIN',7:'CODEC_LRCK',8:'CODEC_DOUT',9:None,
        10:'DAC_VREF',11:'DAC_R_P',12:'DAC_L_P',13:'GND',14:None,15:None,
        16:'AUDIO_3V3',17:'AUDIO_3V3',18:'GND',19:'ADC_VREF',20:'CODEC_VMID',
        21:'MIC_R_N',22:'MIC_R_P',23:'MIC_L_N',24:'MIC_L_P',25:None,26:'CODEC_AD0',
        27:'SYSTEM_I2C_SDA',28:'SYSTEM_I2C_SCL',29:'GND'}.items()})
    # Keep the existing 100k resistor as a harmless ADC data pull-down.
    change('R806',nets={'1':'CODEC_DOUT','2':'GND'})
    for ref in ['C803','C804','C805']:change(ref,value='100n 10V')
    for ref in ['C806','C807']:change(ref,value='10u 10V')
    for ref in ['C809','C811']:
        change(ref,nets={'1':'GND','2':p[ref]['nets']['2']})
    for ref,value,net,xy,pcb in [
        ('C879','100n 10V','AUDIO_3V3',(190.5,223.52),(127,61)),
        ('C880','10u 10V','AUDIO_3V3',(246.38,223.52),(130,61)),
        ('C881','100n 10V','ADC_VREF',(302.26,223.52),(127,64)),
        ('C882','100n 10V','CODEC_VMID',(358.14,223.52),(130,64))]:
        cap(ref,value,net,'GND',xy,pcb,'11_codec')
    d.sheets['11_codec']='11 ES8388 stereo codec'
    d.notes['11_codec']=[
        'ES8388 QFN28 replaces ES8389. 0x10 I2C; standard I2S slave on existing GPIO9..13.',
        'Dual differential ADC: LIN1-RIN1=left mic; LIN2-RIN2=right mic. Reg10=0xFC, Reg11.DS=0.',
        'LOUT1/ROUT1 feed the stereo line path. Negative PA inputs have AC ground returns; unused OUT2 pins are NC.',
        'No ES8389 TDM pin swap or 192kHz support. Port ES8388 initialization; mute PAs during transitions.'
    ]

    box('FSV4730_GU',[(1,'DOUT','output'),(2,'DFS','input'),(3,'GPO3_XTAL','bidirectional'),
        (4,'GPO2_INT','bidirectional'),(5,'GPO1','bidirectional'),(6,'NC1','no_connect'),
        (7,'NC2','no_connect'),(8,'FMI','input'),(9,'RFGND','power_in'),
        (10,'GND_RESERVED1','power_in'),(11,'GND_RESERVED2','power_in'),(12,'AMI','input'),
        (13,'GND1','power_in'),(14,'GND2','power_in'),(15,'RST_N','input'),(16,'SEN','input'),
        (17,'SCLK','input'),(18,'SDIO','bidirectional'),(19,'RCLK','input'),
        (20,'VD','power_in'),(21,'VA','power_in'),(22,'DBYP','passive'),
        (23,'ROUT','output'),(24,'LOUT','output')])
    change('U815',kind='FSV4730_GU',value='FSV4730-D60-GU',
        footprint='Carrier:FSV4730_SSOP24',source=FM,
        status='FM stereo only; NO RDS on 4730. SEN low: 0x11. Hold I2C idle for 1s after power-on; port driver.',
        nets={str(k):v for k,v in {1:None,2:'GND',3:'FM_XTAL_OUT',4:None,5:None,6:None,7:None,
        8:'FM_ANT_IN',9:'GND',10:'GND',11:'GND',12:'GND',13:'GND',14:'GND',15:'FM_RESET_N',
        16:'GND',17:'FM_SCL',18:'FM_SDA',19:'FM_RCLK',20:'FM_3V0',21:'FM_3V0',
        22:'FM_DBYP',23:'FM_R',24:'FM_L'}.items()})
    change('Y810',value='SXTF3215R32768A20F',nets={'1':'FM_RCLK','2':'FM_XTAL_OUT'},
        footprint='Carrier:SXTF3215R_32k',source='https://item.szlcsc.com/23941597.html',
        status='32.768kHz, 12.5pF, ESR<=50k; 22pF legs plus board capacitance; measure frequency/startup')
    change('C822',value='1n C0G')
    box('MCP120_TT',[(1,'RESET_N','open_collector'),(2,'VDD','power_in'),(3,'VSS','power_in')])
    add('12_fm_audio','U816','MCP120_TT','MCP120T-270I/TT',
        {1:'FM_RESET_N',2:'FM_3V0',3:'GND'},(76.2,279.4),(114,65),
        'Carrier:SOT23_MCP120','https://ww1.microchip.com/downloads/en/DeviceDoc/11184d.pdf')
    add('12_fm_audio','R821','R','10k',{1:'FM_3V0',2:'FM_RESET_N'},
        (190.5,251.46),(118,65),'Resistor_SMD:R_0603_1608Metric')
    for ref,value,net,xy,pcb in [
        ('C823','100n 10V','FM_3V0',(246.38,223.52),(103,61)),
        ('C824','470n 25V','FM_DBYP',(302.26,223.52),(108,61)),
        ('C825','22p C0G','FM_RCLK',(358.14,223.52),(112,69)),
        ('C826','22p C0G','FM_XTAL_OUT',(190.5,279.4),(117,69)),
        ('C827','100n 10V','FM_3V0',(246.38,279.4),(116,62))]:cap(ref,value,net,'GND',xy,pcb)
    # Move crystal / antenna drawing to the unused lower row, clear new parts.
    change('Y810',xy=[302.26,251.46]);change('J810',xy=[302.26,279.4]);change('D810',xy=[358.14,279.4])
    d.sheets['12_fm_audio']='12 FSV4730 FM and stereo source'
    d.notes['12_fm_audio']=[
        'Onboard FSV4730-D60-GU replaces unavailable RDA5807M; analog FM stereo, 64..108MHz; no RDS.',
        'SEN=GND selects 0x11; ES8388=0x10, OPT3001=0x45, SHT40=0x44: no I2C address collision.',
        'MCP120 delays reset release (700ms max). Hold system I2C idle for 1s after enabling FM power, then poll CTS.',
        'Crystal connects RCLK to GPO3, with 22pF loads. Firmware enables crystal mode; unused AM input grounded.',
        'Existing independent stereo selector and antenna ESD retained. Port FM driver and verify RF on hardware.'
    ]
    change('U1301',value='SHT40-AD1F-R2',footprint='Carrier:SHT40_AD1F_DFN4_Membrane',source=SHT,
        status='PTFE membrane version; same pinout/footprint/address; height 0.69mm; keep vent uncovered')
    d.notes['17_cabin_sensors']=[n.replace('SHT40-AD1B-R2','SHT40-AD1F-R2') for n in d.notes['17_cabin_sensors']]
    for ref in ['D211','D212','D310','D311']:
        change(ref,value='BAT54H,115',footprint='Carrier:BAT54H_SOD123F',source='https://assets.nexperia.com/documents/data-sheet/BAT54H.pdf')
    change('R109',value='NTC 10k B3950 1%',source='https://item.szlcsc.com/51854434.html',
        status='ST10603X103F3950FB; R25 and B25/50 each +/-1%; board sensor, not battery-pack NTC')
    for ref in ['SW101','SW102']:
        change(ref,value='TS-038-060250-D2',kind='SIDE_KEY_SH',footprint='Carrier:TS038_SideKey',
            nets={**p[ref]['nets'],'SH':'GND'},source='https://item.szlcsc.com/54999684.html')
    box('SIDE_KEY_SH',[(1,'A','passive'),(2,'B','passive'),('SH','SHELL','passive')])
    box('SLIDE_DPDT',[(1,'A_OFF','passive'),(2,'A_COM','passive'),(3,'A_ON','passive'),
        (4,'B_OFF','passive'),(5,'B_COM','passive'),(6,'B_ON','passive'),('SH','SHELL','passive')])
    change('SW310',value='SK22D02L4',kind='SLIDE_DPDT',footprint='Carrier:SK22D02L4_SideSlide',
        nets={'1':'GND','2':'SLIDER_ON','3':'SYS_ALWAYS','4':None,'5':None,'6':None,'SH':'GND'},
        source='https://item.szlcsc.com/639830.html',status='Side actuator L=4mm; use pole A only; second pole NC; housing opening needs refit')
    change('L300',value='1uH CXP0520-1R0M-AG',footprint='Carrier:L_DMBJ_CXP0520',
        source='https://item.szlcsc.com/8489120.html',status='Isat 9A/Irms 8A characterization; 17mOhm max; hot saturation/load test pending')
    for ref,value in [('L410','2.2uH MWSA0402S-2R2MT'),('L1010','1uH MWSA0402S-1R0MT'),('L820','1uH MWSA0402S-1R0MT')]:
        change(ref,value=value,footprint='Carrier:L_Sunlord_MWSA0402S',
            source='https://item.szlcsc.com/'+('397737' if ref=='L410' else '397734')+'.html',
            status='2023 datasheet: min Isat 4.0A (2.2uH), 5.6A (1uH); thermal validation pending')
    d.official('CPOL','Device','C_Polarized')
    for ref in ['C1012','C1013']:
        change(ref,kind='CPOL',value='220u 10V polymer 25mR',footprint='Carrier:T520D_7343_31',
            source='https://item.szlcsc.com/117998.html',status='T520D227M010ATE025; polarized polymer, 25mOhm max at 100kHz; 3.1mm maximum height')
    # Fit the added FM support parts above the A3 title block. Put the crystal
    # and antenna items on a separate last row; reset supervisor has a free
    # lower-left column. This is schematic geometry, not PCB placement.
    fm=[z for z in d.parts if z['sheet']=='12_fm_audio']
    regular=[z for z in fm if not z['ref'].startswith('U') and z['ref'] not in ['Y810','J810','D810']]
    byref={z['ref']:z for z in fm}
    ordered=regular+[byref[r] for r in ['Y810','J810','D810']]
    for i,z in enumerate(ordered):z['xy']=[190.5+(i%4)*55.88,55.88+(i//4)*25.4]
    byref['U816']['xy']=[190.5,261.62]
    d.notes['18_dual_mic']=[n.replace('60mm nominal center spacing','131mm nominal center spacing') for n in d.notes['18_dual_mic']]
