"""Cabin sensors on the existing switched 3V3 system I2C bus.

Pin maps follow TI OPT3001 Rev C and Sensirion SHT4x v7.3. The board NTC
remains a separate protection measurement, not a cabin temperature sensor.
"""


def add_sensors(d, page, box, item, pas, cap):
    s = '17_cabin_sensors'
    page(s, '17 Cabin ambient light and humidity', (60, 8), [
        'OPT3001DNPR: ADDR=3V3 selects 0x45; SHT40-AD1B-R2 fixed 0x44. SYSTEM_I2C, no extra GPIO.',
        'Reuse R110/R111 4.7k bus pull-ups. R1300/R1301 are 22R branch damping; start at 100kHz.',
        'U1300 needs a housing optical window/light guide and LCD stray-light baffle; calibrate assembled optics.',
        'U1301 needs a vented isolated pocket, PCB thermal slots and no copper pour. Heater OFF for cabin readings.',
        'These are populated sensor circuits; housing response, optical calibration and firmware are unvalidated.'
    ])
    box('OPT3001_DNP', [(1,'VDD','power_in'), (2,'ADDR','input'), (3,'GND','power_in'),
        (4,'SCL','input'), (5,'INT_N','open_collector'), (6,'SDA','bidirectional'),
        (7,'EP_GND','power_in')])
    item(s, 'U1300', 'OPT3001_DNP', 'OPT3001DNPR',
         {1:'+3V3', 2:'+3V3', 3:'GND', 4:'CABIN_I2C_SCL', 5:None,
          6:'CABIN_I2C_SDA', 7:'GND'},
         'Carrier:OPT3001_DNP0006A', xy=(76.2,83.82), pcb=(58,4),
         source='https://www.ti.com/lit/ds/symlink/opt3001.pdf',
         status='0x45, polled; optical window and assembled calibration required')
    box('SHT40', [(1,'SDA','bidirectional'), (2,'SCL','input'),
        (3,'VDD','power_in'), (4,'VSS','power_in')])
    item(s, 'U1301', 'SHT40', 'SHT40-AD1B-R2',
         {1:'CABIN_I2C_SDA', 2:'CABIN_I2C_SCL', 3:'+3V3', 4:'GND'},
         'Carrier:SHT40_DFN4_NoCentralPad',
         xy=(76.2,147.32), pcb=(122,80),
         source='https://sensirion.com/media/documents/33FD6951/6A7C10A0/HT_DS_Datasheet_SHT4x_V7.3.pdf',
         status='0x44; no center pad; vented thermal island, heater normally off')
    pas(s, 'R1300', '22R', 'SYSTEM_I2C_SDA', 'CABIN_I2C_SDA', pcb=(60,10))
    pas(s, 'R1301', '22R', 'SYSTEM_I2C_SCL', 'CABIN_I2C_SCL', pcb=(63.5,10))
    cap(s, 'C1300', '100n 10V X7R', '+3V3', pcb=(58,7.5))
    cap(s, 'C1301', '100n 10V X7R', '+3V3', pcb=(122,77))
    cap(s, 'C1302', '1u 10V X7R', '+3V3', pcb=(54.5,7.5))
