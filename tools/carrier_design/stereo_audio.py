"""Revision H audio: S31-Korvo-1 ES8389 topology adapted to the P4 carrier.

The official board uses the pin-4 TDM data / pin-9 clock multiplexing.
AD1=1, AD0=0 selects 7-bit 0x12, avoiding RDA5807M 0x10/0x11.
Two matched top-port analog MEMS microphones are soldered directly to the PCB.
"""

KORVO='https://dl.espressif.com/schematics/esp32-s31-korvo-1-schematics.pdf'
ES='https://atta.szlcsc.com/upload/public/pdf/source/20250902/D83DF6C36B2B03B18C523F480A71BD56.pdf'
NS='https://www.micros.com.pl/mediaserver/ULNS4150b_NSIWAY_0001.pdf'
SWITCH='https://www.ti.com/lit/ds/symlink/sn74lvc1g3157.pdf'
MIC='https://datasheet.lcsc.com/datasheet/pdf/922c86cdb9d20ace7f31ad0e941a76e0.pdf?productCode=C54320678'
LDO='https://datasheet.lcsc.com/datasheet/pdf/c9ef11d769b31cb43b095af03eae2458.pdf?productCode=C403651'


def add_audio(d,page,box,item,pas,cap,mos,tp):
    s='11_codec'
    page(s,'11 ES8389 stereo codec',(9,60),[
        'S31-Korvo-1 V1.1 audio topology, adapted to P4 GPIO9..13. ES8389 is NOT pin-compatible with ES8311.',
        'TDM wiring: pin4=playback data, pin9=MCLK. Firmware must configure the pin multiplexers and stereo slots.',
        'AD1 pull HIGH / AD0 LOW: 7-bit I2C 0x12 (8-bit write 0x24); avoids RDA5807M 0x10/0x11.',
        'Two ADC inputs remain microphones. FM bypasses codec; no hardware FM echo-reference ADC is provided.'
    ])
    box('ES8389',[(1,'AD0','input'),(2,'CDATA','bidirectional'),(3,'CCLK','input'),
        (4,'MCLK_TDMIN','input'),(5,'DVDD','power_in'),(6,'PVDD','power_in'),(7,'DGND','power_in'),
        (8,'SCLK','input'),(9,'DSDIN_CLKIN','input'),(10,'LRCK','input'),(11,'ASDOUT_AD1','bidirectional'),
        (12,'ROUTP','output'),(13,'ROUTN','output'),(14,'DACVREF','passive'),(15,'LOUTN','output'),
        (16,'LOUTP','output'),(17,'AVDD','power_in'),(18,'AGND','power_in'),(19,'VMID','passive'),
        (20,'ADCVREF','passive'),(21,'MIC2N','input'),(22,'MIC2P','input'),(23,'MIC1N','input'),
        (24,'MIC1P','input'),(25,'PGND','power_in')])
    item(s,'U800','ES8389','ES8389',
        {1:'CODEC_AD0',2:'SYSTEM_I2C_SDA',3:'SYSTEM_I2C_SCL',4:'CODEC_TDMIN',5:'AUDIO_3V3',
         6:'AUDIO_3V3',7:'GND',8:'CODEC_BCLK',9:'CODEC_MCLK',10:'CODEC_LRCK',11:'CODEC_DOUT',
         12:'DAC_R_P',13:'DAC_R_N',14:'DAC_VREF',15:'DAC_L_N',16:'DAC_L_P',17:'AUDIO_3V3',
         18:'GND',19:'CODEC_VMID',20:'ADC_VREF',21:'MIC_R_N',22:'MIC_R_P',23:'MIC_L_N',
         24:'MIC_L_P',25:'GND'},'Carrier:ES8389_QFN24_3x3_P0.35',
         xy=(76.2,99.06),pcb=(22,65),source=ES,status='S31 TDM pin mux; 0x12; firmware port required')
    item(s,'U801','AP2112K','AP2112K-3.3TRG1',{1:'+5V',2:'GND',3:'+5V',4:None,5:'AUDIO_3V3'},
         'Package_TO_SOT_SMD:SOT-23-5',xy=(76.2,175.26),pcb=(11,61))
    for i,(a,b) in enumerate([('I2S_MCLK','CODEC_MCLK'),('I2S_BCLK','CODEC_BCLK'),
        ('I2S_LRCK','CODEC_LRCK'),('I2S_DOUT','CODEC_TDMIN'),('CODEC_DOUT','I2S_DIN')]):
        pas(s,'R'+str(800+i),'22R',a,b)
    pas(s,'R805','100k','CODEC_AD0','GND')
    pas(s,'R806','100k','AUDIO_3V3','CODEC_DOUT')
    for i,(value,net) in enumerate([('1u 10V','+5V'),('10u 10V','AUDIO_3V3'),
        ('100n 10V','AUDIO_3V3'),('1u 10V','AUDIO_3V3'),('1u 10V','AUDIO_3V3'),
        ('1u 10V','DAC_VREF'),('1u 10V','ADC_VREF'),('1u 10V','CODEC_VMID')]):cap(s,'C'+str(800+i),value,net)
    for i,net in enumerate(['DAC_L_P','DAC_L_N','DAC_R_P','DAC_R_N']):
        pas(s,'C'+str(808+i),'1u 10V',net,net+'_AC')
        pas(s,'R'+str(807+i),'100k','AUDIO_MID',net+'_AC')

    s='12_fm_audio'
    page(s,'12 RDA5807M and stereo source',(33,62),[
        'Onboard RDA5807M MSOP10 with 32.768k crystal. J810 is FM antenna/GND, not the former 8-pin module port.',
        'FM L/R remain independent. Four SN74LVC1G3157 switches select both legs of both speaker channels.',
        'AUDIO_SOURCE_SELECT=0: codec (pin3); =1: FM (pin1). Mute both PAs before changing source or FM power.',
        'RDA5807M supply and I2C branch are switched together. Antenna matching/ESD and RF desense need bench QA.'
    ])
    box('RDA5807M',[(1,'GND1','power_in'),(2,'FMIN','input'),(3,'GND2','power_in'),
        (4,'SCLK','input'),(5,'SDIO','bidirectional'),(6,'RCLK','input'),(7,'VDD','power_in'),
        (8,'GND3','power_in'),(9,'ROUT','output'),(10,'LOUT','output')])
    item(s,'U815','RDA5807M','RDA5807M',{1:'GND',2:'FM_ANT_IN',3:'GND',4:'FM_SCL',5:'FM_SDA',
        6:'FM_RCLK',7:'FM_3V0',8:'GND',9:'FM_R',10:'FM_L'},'Package_SO:MSOP-10_3x3mm_P0.5mm',
        xy=(76.2,78.74),pcb=(41,67),source='docs/RDA5807M datasheet_v1.pdf')
    box('ME6211C',[(1,'VIN','power_in'),(2,'VSS','power_in'),(3,'CE','input'),
                   (4,'NC','no_connect'),(5,'VOUT','power_out')])
    item(s,'U810','ME6211C','ME6211C30M5G-N',{1:'AUDIO_3V3',2:'GND',3:'FM_POWER_EN',
        4:None,5:'FM_3V0'},'Package_TO_SOT_SMD:SOT-23-5',xy=(76.2,121.92),pcb=(35,63),
        source=LDO,status='3.0V 1%; 1uF ceramic input/output; verify low-headroom startup and FM noise on bench')
    box('SN74LVC1G3157',[(1,'B2','passive'),(2,'GND','power_in'),(3,'B1','passive'),
                   (4,'A','passive'),(5,'VCC','power_in'),(6,'S','input')])
    for i,(codec,fm,output) in enumerate([
        ('DAC_L_P_AC','FM_L_BIASED','AMP_L_P_AC'),('DAC_L_N_AC','AUDIO_MID','AMP_L_N_AC'),
        ('DAC_R_P_AC','FM_R_BIASED','AMP_R_P_AC'),('DAC_R_N_AC','AUDIO_MID','AMP_R_N_AC')]):
        item(s,'U'+str(811+i),'SN74LVC1G3157','SN74LVC1G3157DCKR',
             {1:fm,2:'GND',3:codec,4:output,5:'AUDIO_3V3',6:'AUDIO_SELECT_LOCAL'},
             'Package_TO_SOT_SMD:SOT-363_SC-70-6',xy=(76.2,160.02+i*33.02),
             pcb=(46+i*4,63),source=SWITCH)
    # R811 is retained; R810 is now codec bias, so new FM control pull-down uses R812.
    pas(s,'R812','100k','FM_POWER_EN','GND');pas(s,'R811','100k','FM_3V0','GND')
    cap(s,'C812','1u 10V','AUDIO_3V3');cap(s,'C813','1u 10V','FM_3V0');cap(s,'C814','22n','FM_3V0')
    for i,(lo,hi) in enumerate([('FM_SDA','SYSTEM_I2C_SDA'),('FM_SCL','SYSTEM_I2C_SCL')]):
        mos(s,'Q'+str(810+i),'FM_3V0',lo,hi);pas(s,'R'+str(813+i),'4.7k','FM_3V0',lo)
    for i,ch in enumerate(['L','R']):
        pas(s,'C'+str(815+i),'2.2u 10V','FM_'+ch,'FM_'+ch+'_BIASED')
        pas(s,'R'+str(815+i),'100k','AUDIO_MID','FM_'+ch+'_BIASED')
    pas(s,'R817','10k','AUDIO_3V3','AUDIO_MID');pas(s,'R818','10k','AUDIO_MID','GND')
    cap(s,'C817','10u 10V','AUDIO_MID',fp='Capacitor_SMD:C_0805_2012Metric')
    pas(s,'R819','1k','AUDIO_SOURCE_SELECT','AUDIO_SELECT_LOCAL');pas(s,'R820','100k','AUDIO_SELECT_LOCAL','GND')
    for i in range(4):cap(s,'C'+str(818+i),'100n','AUDIO_3V3')
    box('XTAL2',[(1,'X1','passive'),(2,'X2','passive')],5.08)
    item(s,'Y810','XTAL2','ABS07-32.768KHZ-7-T',{1:'FM_RCLK',2:'GND'},
         'Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm',xy=(190.5,251.46),pcb=(40,73),
         status='RDA DCXO self-bias, CL=7pF; no generic two-pin MCU Pierce capacitors')
    item(s,'J810','CONN2','FM ANT / GND',{1:'FM_ANT',2:'GND'},
         'Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical',xy=(246.38,251.46),pcb=(33,72))
    pas(s,'C822','100p C0G','FM_ANT','FM_ANT_IN',pcb=(36,69))
    # Low-capacitance bidirectional antenna ESD clamp; official Device:D_TVS symbol.
    d.official('D_TVS','Device','D_TVS')
    item(s,'D810','D_TVS','PESD5V0U1BA',{1:'FM_ANT',2:'GND'},'Diode_SMD:D_SOD-323',
         xy=(302.26,251.46),pcb=(35,72),source='https://assets.nexperia.com/documents/data-sheet/PESD5V0U1BA.pdf')

    s='18_dual_mic'
    page(s,'18 Dual onboard microphones',(8,64),[
        'Two LMA3729T381-OAS02 top-port MEMS microphones solder directly to the F face, toward rear housing.',
        'U802=left, U803=right. Same part/orientation; 60mm nominal center spacing, two separate housing ports.',
        'Codec senses each microphone OUT against its local ground via a matched AC-coupled pair.',
        'Keep acoustic ports clear of solder paste, adhesive and coating; no board washing or ultrasonic cleaning.'
    ])
    box('LMA3729T381_OAS02',[(1,'VDD','power_in'),(2,'OUT','output'),(3,'GND1','power_in'),(4,'GND2','power_in')])
    for i,ch in enumerate(['L','R']):
        item(s,'U'+str(802+i),'LMA3729T381_OAS02','LMA3729T381-OAS02',
             {1:'MIC_'+ch+'_3V3',2:'MIC_'+ch+'_OUT',3:'GND',4:'GND'},
             'Carrier:LMA3729T381_OAS02_TopPort',xy=(76.2,83.82+i*68.58),pcb=(20+i*60,73),
             source=MIC)
        pas(s,'R'+str(840+i),'100R','AUDIO_3V3','MIC_'+ch+'_3V3')
        cap(s,'C'+str(840+i*4),'1u 10V','MIC_'+ch+'_3V3')
        pas(s,'C'+str(841+i*4),'1u 10V','MIC_'+ch+'_OUT','MIC_'+ch+'_P')
        pas(s,'C'+str(842+i*4),'1u 10V','GND','MIC_'+ch+'_N')
        pas(s,'C'+str(843+i*4),'100p C0G','MIC_'+ch+'_P','MIC_'+ch+'_N')
        cap(s,'C'+str(848+i),'100n 10V','MIC_'+ch+'_3V3')
        item(s,'#FLG'+str(840+i),'PWR_FLAG','PWR_FLAG',{1:'MIC_'+ch+'_3V3'},xy=(50.8+i*50.8,226.06))

    s='19_stereo_pa'
    page(s,'19 Dual speaker amplifiers',(90,64),[
        'S31-Korvo-1 dual NS4150B structure. J820/J821 are independent BTL outputs; neither terminal is GND.',
        'Separate 4.68V buck rail (legacy net PA_4V8) stays below 5V across static tolerance; see procurement audit.',
        '100k input resistors: nominal differential gain 240k/100k=2.4 (7.6dB). Start volume low.',
        'AMP_EN low mutes both channels; assert only after rail/codec settle. No 5W or simultaneous full-load claim.'
    ])
    box('NS4150B',[(1,'CTRL','input'),(2,'BYPASS','passive'),(3,'INP','input'),(4,'INN','input'),
        (5,'VON','output'),(6,'VCC','power_in'),(7,'GND','power_in'),(8,'VOP','output')])
    for i,ch in enumerate(['L','R']):
        item(s,'U'+str(820+i),'NS4150B','NS4150B',
             {1:'AMP_EN',2:'PA_'+ch+'_BYPASS',3:'PA_'+ch+'_INP',4:'PA_'+ch+'_INN',
              5:'SPK_'+ch+'_N',6:'PA_4V8',7:'GND',8:'SPK_'+ch+'_P'},
             'Package_SO:MSOP-8_3x3mm_P0.65mm',xy=(76.2,76.2+i*48.26),pcb=(94+i*14,67),source=NS)
        item(s,'J'+str(820+i),'CONN2','SPEAKER '+ch+' / 4R',
             {1:'SPK_'+ch+'_P',2:'SPK_'+ch+'_N'},'Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical',
             xy=(76.2,218.44+i*35.56),pcb=(95+i*14,74))
        for j,pol in enumerate(['P','N']):
            pas(s,'C'+str(860+i*8+j),'100n 10V','AMP_'+ch+'_'+pol+'_AC','PA_'+ch+'_CIN_'+pol)
            pas(s,'R'+str(860+i*3+j),'100k 1%','PA_'+ch+'_CIN_'+pol,'PA_'+ch+'_IN'+pol)
            cap(s,'C'+str(865+i*8+j),'1n C0G','SPK_'+ch+'_'+pol)
        pas(s,'R'+str(862+i*3),'10k','AMP_EN','GND')
        cap(s,'C'+str(862+i*8),'1u 10V','PA_'+ch+'_BYPASS')
        cap(s,'C'+str(863+i*8),'100n 10V','PA_4V8')
        cap(s,'C'+str(864+i*8),'10u 10V','PA_4V8',fp='Capacitor_SMD:C_0805_2012Metric')
    item(s,'U822','TLV62585_DRL','TLV62585DRLR',
         {1:'GND',2:'PA_BUCK_SW',3:'+5V',4:'+5V',5:'PA_BUCK_FB',6:None},
         'Package_TO_SOT_SMD:SOT-563',xy=(76.2,172.72),pcb=(100,59),
         source='https://www.ti.com/lit/ds/symlink/tlv62585.pdf')
    pas(s,'L820','1uH Isat>=4.5A','PA_BUCK_SW','PA_4V8',pcb=(105,59),fp='Inductor_SMD:L_Sunlord_SWPA4020S')
    pas(s,'R866','680k 1%','PA_4V8','PA_BUCK_FB');pas(s,'R867','100k 1%','PA_BUCK_FB','GND')
    cap(s,'C876','22u 10V','+5V',fp='Capacitor_SMD:C_0805_2012Metric')
    cap(s,'C877','22u 10V','PA_4V8',fp='Capacitor_SMD:C_0805_2012Metric')
    cap(s,'C878','22u 10V','PA_4V8',fp='Capacitor_SMD:C_0805_2012Metric')
    item(s,'#FLG820','PWR_FLAG','PWR_FLAG',{1:'PA_4V8'},xy=(139.7,251.46))
