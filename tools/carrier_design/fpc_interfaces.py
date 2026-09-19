"""Real FPC interfaces; unresolved module/power details remain explicit holds.

The connector pin numbers are electrical identities, not screen coordinates.
Camera/LCD high speed pairs come from the two actual module specifications.
The six-pin touch port is the existing reference interface, NOT a claim that
the unspecified touch glass bundled with the LCM has that pinout.
"""


def add_interfaces(d, page, box, item, pas, cap):
    s = '14_lcd_fpc'
    page(s, '14 LCD FPC and 1V8 controls', (71, 64), [
        'J1200: LCM-070WS209-A0, PDF pp5-7. 30 pins, 0.5 mm; F side, cable entry from bottom.',
        'Only D0/D1 are connected. Vendor 2-lane initialization and timing remain REQUIRED.',
        'LCD_AVDD_9V8 / VGH / VGL / VCOM and LEDA/K are UNDRIVEN: bias/backlight stage still pending.',
        'Dual-contact connector does not prove folded-tail reach, pin-1 orientation or thickness tolerance.'
    ])
    names = ['LEDA', 'LEDA', 'VGH', 'VGL', 'UPDN', 'SHLR', 'LEDK', 'LEDK', 'AVDD', 'GND',
             'DSI_D3_P_NC', 'DSI_D3_N_NC', 'GND', 'DSI_D2_P_NC', 'DSI_D2_N_NC', 'GND',
             'DSI_CLK_P', 'DSI_CLK_N', 'GND', 'DSI_D1_P', 'DSI_D1_N', 'GND', 'DSI_D0_P',
             'DSI_D0_N', 'GND', 'STBYB_1V8', 'RESET_1V8', 'VDD_1V8', 'VDD_1V8', 'VCOM']
    box('LCM070_FPC30', [(i + 1, name, 'passive') for i, name in enumerate(names)] +
        [('MP', 'MOUNT', 'passive')], 22.86)
    nets = {1:'LCD_LEDA', 2:'LCD_LEDA', 3:'LCD_VGH', 4:'LCD_VGL', 5:'LCD_UPDN',
            6:'LCD_SHLR', 7:'LCD_LEDK', 8:'LCD_LEDK', 9:'LCD_AVDD_9V8', 10:'GND',
            11:None, 12:None, 13:'GND', 14:None, 15:None, 16:'GND',
            17:'DSI_CLK_P', 18:'DSI_CLK_N', 19:'GND', 20:'DSI_D1_P', 21:'DSI_D1_N',
            22:'GND', 23:'DSI_D0_P', 24:'DSI_D0_N', 25:'GND', 26:'LCD_STBYB_1V8',
            27:'LCD_RESET_1V8', 28:'LCD_1V8', 29:'LCD_1V8', 30:'LCD_VCOM', 'MP':'GND'}
    item(s, 'J1200', 'LCM070_FPC30', 'FPC-05FB-30PH20', nets,
         'Carrier:XUNPU_FPC_05FB_30PH20', xy=(71.12, 93.98), pcb=(70.5, 57.5), side='F',
         source='https://www.xunpu.com.cn/product/413.html',
         status='LCD pinout checked; folded-tail fit and connector tolerances unverified')

    box('TXU0102_DCU', [(1,'B2Y','output'), (2,'GND','power_in'), (3,'VCCA','power_in'),
        (4,'A2','input'), (5,'A1','input'), (6,'OE','input'), (7,'VCCB','power_in'), (8,'B1Y','output')])
    item(s, 'U1200', 'AP2112K', 'AP2112K-1.8TRG1',
         {1:'+3V3', 2:'GND', 3:'LCD_VDD_EN', 4:None, 5:'LCD_1V8'},
         'Package_TO_SOT_SMD:SOT-23-5', xy=(71.12, 165.1), pcb=(70, 64),
         source='https://www.diodes.com/assets/Datasheets/AP2112.pdf')
    item(s, 'U1201', 'TXU0102_DCU', 'TXU0102DCUR',
         {1:'LCD_STBYB_1V8', 2:'GND', 3:'+3V3', 4:'LCD_STBYB', 5:'LCD_RESET',
          6:'LCD_1V8', 7:'LCD_1V8', 8:'LCD_RESET_1V8'},
         'Package_SO:VSSOP-8_2.3x2mm_P0.5mm', xy=(71.12, 224.79), pcb=(78, 64),
         source='https://www.ti.com/lit/ds/symlink/txu0102.pdf')
    for ref, val, net in [('C1200','1u 10V X7R','+3V3'), ('C1201','1u 10V X7R','LCD_1V8'),
                          ('C1202','100n 10V X7R','LCD_1V8'), ('C1203','100n 10V X7R','+3V3')]:
        cap(s, ref, val, net)
    for ref, val, net in [('R1200','100k','LCD_VDD_EN'), ('R1201','10k bleed','LCD_1V8'),
                         ('R1202','100k','LCD_RESET'), ('R1203','100k','LCD_STBYB'),
                         ('R1204','100k','LCD_RESET_1V8'), ('R1205','100k','LCD_STBYB_1V8')]:
        pas(s, ref, val, net, 'GND')
    pas(s, 'R1206', '10k scan option', 'LCD_UPDN', 'LCD_1V8')
    pas(s, 'R1207', '10k scan option', 'LCD_SHLR', 'GND')

    s = '15_touch_fpc'
    page(s, '15 Touch FPC reference interface', (89, 67), [
        'J1210: 6-pin reference interface; selected touch module drawing has NOT been supplied.',
        '1 RESET/control, 2 3V3, 3 GND, 4 INT, 5 SDA, 6 SCL. Verify all six before plugging in.',
        'F side, same cable entry direction as LCD. No-touch product leaves external cable absent.',
        '3V3 interface assumption only; contact width/thickness, voltage, polarity and ESD await closure.'
    ])
    box('TOUCH_FPC6', [(1,'RESET_CONTROL','passive'), (2,'VCC_3V3','passive'),
        (3,'GND','passive'), (4,'INT','passive'), (5,'SDA','passive'), (6,'SCL','passive'),
        ('MP','MOUNT','passive')])
    item(s, 'J1210', 'TOUCH_FPC6', 'FPC-05FB-6PH20',
         {1:'TOUCH_RESET_CONN', 2:'+3V3', 3:'GND', 4:'TOUCH_INT_CONN',
          5:'TOUCH_SDA_CONN', 6:'TOUCH_SCL_CONN', 'MP':'GND'},
         'Carrier:XUNPU_FPC_05FB_6PH20', xy=(73.66, 83.82), pcb=(96, 67), side='F',
         source='https://www.xunpu.com.cn/product/413.html',
         status='REFERENCE PINOUT ONLY: selected touch panel drawing and voltage unverified')
    for i, (a,b) in enumerate([('TOUCH_RESET','TOUCH_RESET_CONN'), ('TOUCH_INT','TOUCH_INT_CONN'),
            ('CAM_TOUCH_I2C_SDA','TOUCH_SDA_CONN'), ('CAM_TOUCH_I2C_SCL','TOUCH_SCL_CONN')]):
        pas(s, 'R'+str(1210+i), '100R', a, b)
    for i, net in enumerate(['CAM_TOUCH_I2C_SDA', 'CAM_TOUCH_I2C_SCL']):
        pas(s, 'R'+str(1214+i), '4.7k', '+3V3', net)
    cap(s, 'C1210', '100n 10V X7R', '+3V3')
    cap(s, 'C1211', '1u 10V X7R', '+3V3')

    s = '16_camera_fpc'
    page(s, '16 Camera FPC pin map', (73, 7), [
        'J1220: OSPTEK ESP32-P4-Camera SC2336 specification p3, 24 pins / 0.5 mm / 0.3 mm tail.',
        'CSI pairs connected to core. 1V8 / 2V8 supplies, sequencer, 24MHz and level conversion PENDING.',
        'CAM_*_1V8 labels are isolated interface boundaries: NEVER bridge them straight to 3V3 GPIO.',
        'Pins 1/2/11/12/18 stay NC. No external 1V5 pin exists in this module pin map.'
    ])
    names = ['NC','NC','D1_N','D1_P','GND','CLK_N','CLK_P','GND','D0_N','D0_P',
             'NC','NC','GND','MCLK_1V8','GND','DOVDD_1V8','RESET_1V8','NC','GND',
             'SDA_1V8','SCL_1V8','GND','AVDD_2V8','AVDD_2V8']
    box('SC2336_FPC24', [(i+1,name,'passive') for i,name in enumerate(names)] +
        [('MP','MOUNT','passive')], 20.32)
    item(s, 'J1220', 'SC2336_FPC24', 'FPC-05FB-24PH20',
         {1:None, 2:None, 3:'CSI_D1_N', 4:'CSI_D1_P', 5:'GND', 6:'CSI_CLK_N',
          7:'CSI_CLK_P', 8:'GND', 9:'CSI_D0_N', 10:'CSI_D0_P', 11:None, 12:None,
          13:'GND', 14:'CAM_MCLK_1V8', 15:'GND', 16:'CAM_DOVDD_1V8', 17:'CAM_RESET_1V8',
          18:None, 19:'GND', 20:'CAM_SDA_1V8', 21:'CAM_SCL_1V8', 22:'GND',
          23:'CAM_AVDD_2V8', 24:'CAM_AVDD_2V8', 'MP':'GND'},
         'Carrier:XUNPU_FPC_05FB_24PH20', xy=(73.66, 96.52), pcb=(76.5, 7),
         side='F', angle=180, source='https://github.com/osptek/esp32-p4c5-module-dev-board',
         status='Camera pin map checked; power, MCLK, level conversion and physical mating pending')
