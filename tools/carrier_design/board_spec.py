"""R0 carrier circuit source. Real circuits are separated from release holds.

Run with KiCad 10's Python. Output is a scratch project until independently
verified and promoted. Never overwrite the historical bare-P4 board.
"""
from cad import Design, OUT, ROOT
import csv

d=Design();d.passive_symbols()
RFP='Resistor_SMD:R_0603_1608Metric'
CFP='Capacitor_SMD:C_0603_1608Metric'
SOT='Package_TO_SOT_SMD:SOT-23'
SOT5='Package_TO_SOT_SMD:SOT-23-5'
SOT6='Package_TO_SOT_SMD:SOT-23-6'
TP='TestPoint:TestPoint_Pad_D1.5mm'
TI='https://www.ti.com/lit/ds/symlink/'
cursor={};groups={};positions={}

def page(name,title,origin,notes=()):
    d.sheet(name,title,notes);cursor[name]=0;groups[name]=origin;positions[name]=0

def item(s,ref,kind,value,nets,fp='',xy=None,pcb=None,**kw):
    if xy is None:
        i=cursor[s];cursor[s]+=1
        xy=(190.5+(i%4)*55.88,55.88+(i//4)*27.94)
    if pcb is None and fp:
        i=positions[s];positions[s]+=1;ox,oy=groups[s]
        pcb=[ox+(i%5)*3.2,oy+(i//5)*3.4]
    return d.add(s,ref,kind,value,nets,xy,fp,pcb,**kw)

def box(kind, entries, hw=15.24):
    d.box_symbol(kind,[(str(n),name,typ) for n,name,typ in entries],hw)

def pas(s,ref,value,a,b,pcb=None,fp=None,dnp=False):
    kind=ref[0]
    return item(s,ref,kind,value,{'1':a,'2':b},fp or (RFP if kind=='R' else CFP if kind=='C' else 'Diode_SMD:D_SOD-123' if kind=='D' else 'Inductor_SMD:L_Sunlord_SWPA6045S'),pcb=pcb,dnp=dnp)

def cap(s,ref,value,net,pcb=None,fp=None):return pas(s,ref,value,net,'GND',pcb,fp)
def tp(s,ref,net,pcb=None):return item(s,ref,'TestPoint',net,{'1':net},TP,pcb=pcb)
def flag(s,n,net,xy):return item(s,'#FLG'+str(n),'PWR_FLAG','PWR_FLAG',{'1':net},xy=xy)
def mos(s,ref,g,sn,dn,pcb=None):
    return item(s,ref,'Q_NMOS','2N7002',{'1':g,'2':sn,'3':dn},SOT,pcb=pcb)

# Pin maps here are package-specific, not inferred from similarly named ICs.
box('TXU0202_DCU',[(1,'B2','input'),(2,'GND','power_in'),(3,'VCCA','power_in'),(4,'A2Y','output'),(5,'A1','input'),(6,'OE','input'),(7,'VCCB','power_in'),(8,'B1Y','output')])
box('AP2112K',[(1,'VIN','power_in'),(2,'GND','power_in'),(3,'EN','input'),(4,'NC','no_connect'),(5,'VOUT','power_out')])
box('TPS2553DBV',[(1,'IN','power_in'),(2,'GND','power_in'),(3,'EN','input'),(4,'FAULT_N','open_collector'),(5,'ILIM','passive'),(6,'OUT','power_out')])
d.official('USB_C_USB2','Connector','USB_C_Receptacle_USB2.0_16P')
d.official('CH343P','Interface_USB','CH343P')
# Preserve the official pin names/types but spread power pins onto the sides.
# The generic horizontal labels otherwise collide on its tightly spaced top pins.
from cad import sexp
chpins=[]
for sub in sexp.find_all(d.symbols['CH343P']['tree'],'symbol'):
    for pin in sexp.find_all(sub,'pin'):
        chpins.append((sexp.atoms(sexp.find(pin,'number'))[0],sexp.atoms(sexp.find(pin,'name'))[0],sexp.atoms(pin)[0]))
box('CH343P',sorted(chpins,key=lambda p:int(p[0])))
d.official('SW_SPDT','Switch','SW_SPDT')
d.official('USBLC6','Power_Protection','USBLC6-2SC6')
d.official('LED','Device','LED')
d.official('FUSE','Device','Fuse')
box('SW_SIDE',[(1,'CONTACT_A','passive'),(2,'CONTACT_B','passive'),('MP','CASE','passive')],7.62)

page('01_core','01 Core compatibility',(57,18),[
 'U100: fit ONE OSPTEK P4C5-Core OR P4C6-Core; not hot-swappable.',
 '85/86 and RTC_VBAT = regulated 3.3 V. Pad 66 is an OUTPUT: leave open.',
 'Unimplemented peripheral labels are intentional R0 interface boundaries; see DESIGN_STATUS.md.'
])
gpio={int(r['core_pad']):r['planned_signal'] for r in csv.DictReader((ROOT/'display_board/hardware/display-carrier/GPIO_PLAN.csv').open(encoding='utf-8-sig')) if r['core_pad'].isdigit()}
special={1:'GND',2:None,3:'GND',4:'RF_RX',5:'RF_TX',6:'RF_BOOT_C5',7:'RF_STRAP_C5',8:None,9:'RF_BOOT_C6',10:'RF_STRAP_C6',11:None,12:None,13:None,14:None,15:'GND',33:'GND',34:'DSI_D1_P',35:'DSI_D1_N',36:'DSI_CLK_N',37:'DSI_CLK_P',38:'DSI_D0_P',39:'DSI_D0_N',40:'GND',41:'CSI_D0_N',42:'CSI_D0_P',43:'CSI_CLK_P',44:'CSI_CLK_N',45:'CSI_D1_N',46:'CSI_D1_P',47:'GND',48:'MODEM_USB_N',49:'MODEM_USB_P',52:'GND',66:None,83:'GND',84:'+3V3',85:'+3V3',86:'+3V3',87:'P4_EN'}
special.update({21:None,61:None,82:'RF_EN_SERVICE'})
core_nets={str(n):special[n] if n in special else gpio[n] for n in range(1,89)}
core_nets['81']='TYPEC_HIGH_CURRENT_N'
pinrows=list(csv.DictReader((ROOT/'display_board/hardware/display-carrier/CORE_PIN_COMPATIBILITY.csv').open(encoding='utf-8-sig')))
corepins=[]
for n in range(1,89):
    net=core_nets[str(n)];typ='power_in' if n in [1,3,15,33,40,47,52,83,84,85,86] else 'power_out' if n==66 else 'input' if n==87 else 'bidirectional'
    label=net or ('LDO_VO4_OUT' if n==66 else 'RESERVED_'+str(n))
    corepins.append((n,label,typ))
box('OSPTEK_P4C5_P4C6',corepins,25.4)
item('01_core','U100','OSPTEK_P4C5_P4C6','OSPTEK P4C5 / P4C6',core_nets,'Carrier:OSPTEK_P4C5_P4C6_Compatible',xy=(95.25,137.16),pcb=(76,36),source='https://github.com/osptek',angle=180)
for i,(v,n) in enumerate([('22u 10V X5R','+3V3'),('22u 10V X5R','+3V3'),('100n 10V X7R','+3V3'),('100n 10V X7R','+3V3')]):cap('01_core','C'+str(101+i),v,n,fp='Capacitor_SMD:C_0805_2012Metric' if i<2 else None)
for n,net in [(101,'P4_EN'),(102,'P4_BOOT'),(103,'P4_BOOT_MODE_HIGH')]:pas('01_core','R'+str(n),'10k 1%','+3V3',net)
cap('01_core','C105','1u 10V X7R','P4_EN')
pas('01_core','R104','0R C6 default','RF_BOOT_SERVICE','RF_BOOT_C6')
pas('01_core','R105','0R C5 only','RF_BOOT_SERVICE','RF_BOOT_C5',dnp=True)
pas('01_core','R106','10k C6 strap','+3V3','RF_STRAP_C6')
pas('01_core','R107','10k C5 only','+3V3','RF_STRAP_C5',dnp=True)
for i,n in enumerate(['GND','+3V3','RF_RX','RF_TX','RF_EN_SERVICE','RF_BOOT_SERVICE','P4_EN']):tp('01_core','TP'+str(101+i),n,pcb=(55+i*2.6,75))
for i,net in enumerate(['P4_EN','P4_BOOT']):
    item('01_core','SW'+str(101+i),'SW_SPST','RESET' if i==0 else 'BOOT',{'1':net,'2':'GND'},'Button_Switch_THT:SW_Tactile_SPST_Angled_PTS645Vx39-2LFS',pcb=(101+i*11,3),angle=180)
pas('01_core','R108','10k 1%','+3V3','BOARD_TEMP_ADC')
pas('01_core','R109','NTC 10k B3950','BOARD_TEMP_ADC','GND')
cap('01_core','C106','100n','BOARD_TEMP_ADC')
for i,net in enumerate(['SYSTEM_I2C_SDA','SYSTEM_I2C_SCL','CAM_TOUCH_I2C_SDA','CAM_TOUCH_I2C_SCL']):pas('01_core','R'+str(110+i),'4.7k 1%','+3V3',net)

page('02_usb','02 USB-C and UART',(10,32),[
 'CH343P VIO is USB_AUX3V3: USB enumeration works while the product is off.',
 'TXU0202 provides powered-off isolation of BOTH UART directions.',
 'Program USB descriptor for 500 mA total. ACT_N deasserts during suspend.'
])
usb={str(n):'GND' for n in ['A1','A12','B1','B12','SH']}
usb.update({n:'VBUS_RAW' for n in ['A4','A9','B4','B9']});usb.update({'A5':'CC1','B5':'CC2','A6':'UART_USB_P','B6':'UART_USB_P','A7':'UART_USB_N','B7':'UART_USB_N','A8':None,'B8':None})
assert set(usb)==set(d.symbols['USB_C_USB2']['pins']),d.symbols['USB_C_USB2']['pins']
item('02_usb','J200','USB_C_USB2','USB-C CHARGE / DEBUG',usb,'Carrier:USB_C_HRO_TYPE_C_31_M_12_Edge',xy=(64.77,86.36),pcb=(3.2,31),angle=90)
item('02_usb','U200','CH343P','CH343P',{1:'USB_AUX3V3',2:'GND',3:'VBUS_RAW',4:'BRIDGE_TX',5:'BRIDGE_RX',6:'CH343_V3',7:'UART_USB_P',8:'UART_USB_N',9:'VBUS_RAW',10:'USB_CONFIG_N',11:None,12:'BRIDGE_DTR_N',13:'BRIDGE_RTS_N',14:None,15:None,16:None,17:'GND'},'Package_DFN_QFN:WCH_QFN-16-1EP_3x3mm_P0.5mm_EP1.8x1.8mm',xy=(64.77,162.56),pcb=(15,31),source='https://files.waveshare.com/wiki/ESP32-S3-LCD-1.3/CH343DS1_EN.pdf')
item('02_usb','U201','TXU0202_DCU','TXU0202DCUR',{1:'P4_UART_TX',2:'GND',3:'USB_AUX3V3',4:'BRIDGE_RX',5:'BRIDGE_TX',6:'USB_AUX3V3',7:'+3V3',8:'P4_UART_RX'},'Package_SO:VSSOP-8_2.3x2mm_P0.5mm',xy=(64.77,221),pcb=(22,33),source=TI+'txu0202.pdf')
item('02_usb','U202','USBLC6','USBLC6-2SC6',{1:'UART_USB_P',2:'GND',3:'UART_USB_N',4:'UART_USB_N',5:'VBUS_RAW',6:'UART_USB_P'},SOT6,pcb=(8,31))
for i,(v,n) in enumerate([('100n','CH343_V3'),('100n','VBUS_RAW'),('100n','USB_AUX3V3'),('100n','USB_AUX3V3'),('100n','+3V3'),('1u 10V','VBUS_RAW')]):cap('02_usb','C'+str(201+i),v,n)
for i,(col,em,base) in enumerate([('P4_EN','BRIDGE_DTR_N','BRIDGE_RTS_N'),('P4_BOOT','BRIDGE_RTS_N','BRIDGE_DTR_N')]):
    nb='AUTO_DL_B'+str(i)
    item('02_usb','Q'+str(201+i),'Q_NPN','MMBT3904',{1:nb,2:em,3:col},SOT)
    pas('02_usb','R'+str(201+i),'10k',base,nb)
pas('02_usb','R203','100k','USB_AUX3V3','USB_CONFIG_N')
flag('02_usb',200,'VBUS_RAW',(30.48,254));flag('02_usb',201,'GND',(76.2,254))

page('03_usb_current','03 USB input qualification',(10,11),[
 'No charger input until USB ACT_N is low OR Type-C advertises >= 1.5 A.',
 'TPS2553 worst-case ceiling: 439 mA legacy / 1253 mA Type-C, plus bridge current.',
 'USB 3 A declaration is conservatively treated as 1.5 A. No USB-PD or BC1.2 probing.'
])
box('TUSB320LAI',[(1,'CC1','bidirectional'),(2,'CC2','bidirectional'),(3,'PORT','input'),(4,'VBUS_DET','input'),(5,'ADDR','input'),(6,'INT_N_OUT3','open_collector'),(7,'SDA_OUT1','open_collector'),(8,'SCL_OUT2','open_collector'),(9,'ID','open_collector'),(10,'GND','power_in'),(11,'EN_N','input'),(12,'VDD','power_in')])
item('03_usb_current','U210','TUSB320LAI','TUSB320LAIRWBR',{1:'CC1',2:'CC2',3:'GND',4:'CC_VBUS_DET',5:None,6:None,7:'CC_HIGH_N',8:None,9:None,10:'GND',11:'GND',12:'USB_AUX3V3'},'Carrier:TUSB320_RWB12',xy=(63.5,86.36),pcb=(15,17),source=TI+'tusb320lai.pdf')
item('03_usb_current','U211','AP2112K','AP2112K-3.3TRG1',{1:'VBUS_RAW',2:'GND',3:'VBUS_RAW',4:None,5:'USB_AUX3V3'},SOT5,xy=(63.5,137.16),pcb=(12,23),source='https://www.diodes.com/assets/Datasheets/AP2112.pdf')
item('03_usb_current','U212','TPS2553DBV','TPS2553DBVR',{1:'VBUS_RAW',2:'GND',3:'CHARGE_INPUT_EN',4:'INPUT_FAULT_N',5:'USB_ILIM',6:'CHARGE_VBUS'},SOT6,xy=(63.5,190.5),pcb=(21,18),source=TI+'tps2553.pdf')
pas('03_usb_current','R211','887k 1%','VBUS_RAW','CC_VBUS_DET')
pas('03_usb_current','R212','10k','USB_AUX3V3','CC_HIGH_N')
pas('03_usb_current','R213','10k','USB_AUX3V3','INPUT_ALLOWED_N')
# Diode symbol pin 1 = cathode, 2 = anode. Either low status pulls the AND node low.
pas('03_usb_current','D211','BAT54H','CC_HIGH_N','INPUT_ALLOWED_N',fp='Diode_SMD:D_SOD-123')
pas('03_usb_current','D212','BAT54H','USB_CONFIG_N','INPUT_ALLOWED_N',fp='Diode_SMD:D_SOD-123')
mos('03_usb_current','Q211','INPUT_ALLOWED_N','GND','CHARGE_INPUT_EN')
pas('03_usb_current','R214','47k','USB_AUX3V3','CHARGE_INPUT_EN')
pas('03_usb_current','R215','68.1k 1%','USB_ILIM','GND')
mos('03_usb_current','Q212','CC_HIGH_N','GND','CC_HIGH')
pas('03_usb_current','R216','47k','USB_AUX3V3','CC_HIGH')
mos('03_usb_current','Q213','CC_HIGH','GND','USB_ILIM_BOOST')
pas('03_usb_current','R217','33.2k 1%','USB_ILIM','USB_ILIM_BOOST')
# Read-only core status, isolated by MOS gate; no pullup from USB domain into off P4.
mos('03_usb_current','Q214','CC_HIGH','GND','TYPEC_HIGH_CURRENT_N')
pas('03_usb_current','R218','10k','+3V3','TYPEC_HIGH_CURRENT_N')
pas('03_usb_current','R219','10k','USB_AUX3V3','INPUT_FAULT_N')
tp('03_usb_current','TP210','INPUT_FAULT_N',pcb=(25,13))
for i,(v,n) in enumerate([('100n','USB_AUX3V3'),('1u 10V','USB_AUX3V3'),('1u 10V','VBUS_RAW'),('100n','VBUS_RAW'),('1u 10V','CHARGE_VBUS')]):cap('03_usb_current','C'+str(211+i),v,n)

page('04_charger','04 Battery charger / power path',(25,6),[
 'Pack: protected 1S 4.2 V / 5000 mAh / >=10 A discharge. Separate NTC connector.',
 'R0 charge-disable strap fitted. Charger enable is a release HOLD until pack/NTC qualified.',
 'PSEL defaults to USB 500mA. Higher input current requires qualified Type-C status and firmware.',
 'TS: pack 10k/B3950, R25/B tolerance <=1%. Calculated inhibit approx 2..4 / 41..43 C.'
])
box('BQ25896',[(1,'VBUS','power_in'),(2,'PSEL','input'),(3,'PG_N','open_collector'),(4,'STAT_N','open_collector'),(5,'SCL','input'),(6,'SDA','bidirectional'),(7,'INT_N','open_collector'),(8,'OTG','input'),(9,'CE_N','input'),(10,'ILIM','passive'),(11,'TS','input'),(12,'QON_N','input'),(13,'BAT','passive'),(14,'BAT','passive'),(15,'SYS','passive'),(16,'SYS','passive'),(17,'PGND','power_in'),(18,'PGND','power_in'),(19,'SW','power_out'),(20,'SW','passive'),(21,'BTST','passive'),(22,'REGN','power_out'),(23,'PMID','passive'),(24,'NC','no_connect'),(25,'EP_GND','power_in')])
item('04_charger','U300','BQ25896','BQ25896RTWR',{1:'CHARGE_VBUS',2:'USB_AUX3V3',3:'CHG_PG_N',4:'CHG_STAT_N',5:'SYSTEM_I2C_SCL',6:'SYSTEM_I2C_SDA',7:'CHARGER_INT',8:'GND',9:'CHG_DISABLE',10:'BQ_ILIM',11:'BAT_TS',12:'BQ_QON_N',13:'BAT_PROTECTED',14:'BAT_PROTECTED',15:'SYS_ALWAYS',16:'SYS_ALWAYS',17:'GND',18:'GND',19:'BQ_SW',20:'BQ_SW',21:'BQ_BTST',22:'BQ_REGN',23:'BQ_PMID',24:None,25:'GND'},'Package_DFN_QFN:Texas_RTW_WQFN-24-1EP_4x4mm_P0.5mm_EP2.7x2.7mm',xy=(69.85,96.52),pcb=(33,15),source=TI+'bq25896.pdf')
box('CONN2',[(1,'1','passive'),(2,'2','passive')],5.08)
item('04_charger','J300','CONN2','1S BATTERY 10A',{1:'BAT_CONNECTOR',2:'GND'},'Connector_JST:JST_VH_B2P-VH_1x02_P3.96mm_Vertical',xy=(53.34,162.56),pcb=(30,4))
item('04_charger','J301','CONN2','PACK NTC / RETURN',{1:'BAT_TS',2:'GND'},'Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical',xy=(53.34,213.36),pcb=(43,4))
item('04_charger','F300','FUSE','8A pack qualified fuse',{1:'BAT_CONNECTOR',2:'BAT_PROTECTED'},'Fuse:Fuse_1206_3216Metric',pcb=(30,10),status='HOLD: exact fuse I2t and pack reverse-protection test')
pas('04_charger','L300','1uH Isat>=8A','BQ_SW','SYS_ALWAYS',pcb=(38,15))
for ref,v,net,fp in [('C300','1u 25V','CHARGE_VBUS',CFP),('C301','10u 25V','BQ_PMID','Capacitor_SMD:C_1206_3216Metric'),('C302','4.7u 10V','BQ_REGN',CFP),('C303','10u 10V','BAT_PROTECTED','Capacitor_SMD:C_0805_2012Metric'),('C304','22u 10V','SYS_ALWAYS','Capacitor_SMD:C_0805_2012Metric'),('C305','22u 10V','SYS_ALWAYS','Capacitor_SMD:C_0805_2012Metric')]:cap('04_charger',ref,v,net,fp=fp)
pas('04_charger','C306','47n 16V','BQ_BTST','BQ_SW')
pas('04_charger','R300','300R 1%','BQ_ILIM','GND')
pas('04_charger','R301','9.09k 0.1% / B3950','BQ_REGN','BAT_TS')
pas('04_charger','R302','187k 0.1% / B3950','BAT_TS','GND')
pas('04_charger','R303','0R CHARGE DISABLED','BQ_REGN','CHG_DISABLE')
pas('04_charger','R304','0R ENABLE after review','CHG_DISABLE','GND',dnp=True)
pas('04_charger','R305','10k','+3V3','CHARGER_INT')
pas('04_charger','R306','10k','BAT_PROTECTED','BQ_QON_N')
tp('04_charger','TP300','BQ_QON_N');tp('04_charger','TP301','CHG_PG_N');tp('04_charger','TP302','CHG_STAT_N');tp('04_charger','TP303','BAT_TS')
flag('04_charger',300,'BAT_PROTECTED',(40.64,254));flag('04_charger',301,'SYS_ALWAYS',(91.44,254))

page('05_system_switch','05 Controlled shutdown',(39,25),[
 'TPS22990 EP = VIN (NOT GND). Side slider carries control current only.',
 'OFF requests shutdown; firmware drops POWER_HOLD after LCD/SD/modem quiesce.',
 'Hold-up timeout and reverse-battery protection require closure before production.'
])
box('TPS22990',[(1,'CT','passive'),(2,'NC','no_connect'),(3,'VIN','power_in'),(4,'VBIAS','power_in'),(5,'ON','input'),(6,'GND','power_in'),(7,'PG','open_collector'),(8,'VOUT','power_out'),(9,'VOUT','passive'),(10,'VOUT','passive'),(11,'EP_VIN','power_in')])
item('05_system_switch','U310','TPS22990','TPS22990DMLR',{1:'SYS_SLEW',2:None,3:'SYS_ALWAYS',4:'SYS_ALWAYS',5:'MAIN_ON',6:'GND',7:'MAIN_PG',8:'SYS_SWITCHED',9:'SYS_SWITCHED',10:'SYS_SWITCHED',11:'SYS_ALWAYS'},'Carrier:TPS22990_DML10',xy=(66.04,88.9),pcb=(43,26),source=TI+'tps22990.pdf')
item('05_system_switch','SW310','SW_SPDT','POWER SIDE SLIDE',{1:'GND',2:'SLIDER_ON',3:'SYS_ALWAYS'},'Button_Switch_THT:SW_E-Switch_EG1224_SPDT_Angled',xy=(66.04,154.94),pcb=(127,4),angle=180)
pas('05_system_switch','D310','BAT54H','MAIN_ON','SLIDER_ON',fp='Diode_SMD:D_SOD-123')
pas('05_system_switch','D311','BAT54H','MAIN_ON','POWER_HOLD',fp='Diode_SMD:D_SOD-123')
pas('05_system_switch','R310','100k','MAIN_ON','GND')
pas('05_system_switch','R311','100k','POWER_HOLD','GND')
cap('05_system_switch','C310','10n','SYS_SLEW')
cap('05_system_switch','C311','1u 10V','SYS_ALWAYS')
cap('05_system_switch','C312','10u 10V','SYS_SWITCHED',fp='Capacitor_SMD:C_0805_2012Metric')
mos('05_system_switch','Q310','SLIDER_ON','GND','POWER_OFF_REQUEST')
pas('05_system_switch','R312','10k','+3V3','POWER_OFF_REQUEST')
cap('05_system_switch','C313','10n','POWER_OFF_REQUEST')

page('06_main_power','06 Regulated 5 V / 3.3 V',(25,39),[
 '5 V budget = 4 A PEAK target, not an established continuous rating.',
 'TPS61088 VCC is internal LDO output: bypass only. Feedback uses 1.204 V reference.',
 'Compensation and inductor are engineering values; load-transient / thermal validation required.'
])
box('TPS61088',[(1,'VCC_OUT','power_out'),(2,'EN','input'),(3,'FSW','passive'),(4,'SW','power_out'),(5,'SW','passive'),(6,'SW','passive'),(7,'SW','passive'),(8,'BOOT','passive'),(9,'VIN','power_in'),(10,'SS','passive'),(11,'NC_THERMAL','passive'),(12,'NC_THERMAL','passive'),(13,'MODE','input'),(14,'VOUT','power_out'),(15,'VOUT','passive'),(16,'VOUT','passive'),(17,'FB','input'),(18,'COMP','passive'),(19,'ILIM','passive'),(20,'AGND','power_in'),(21,'EP_PGND','power_in')])
item('06_main_power','U400','TPS61088','TPS61088RHLR',{1:'BOOST_VCC',2:'SYS_SWITCHED',3:'BOOST_FSW',4:'BOOST_SW',5:'BOOST_SW',6:'BOOST_SW',7:'BOOST_SW',8:'BOOST_BOOT',9:'SYS_SWITCHED',10:'BOOST_SS',11:'GND',12:'GND',13:None,14:'+5V',15:'+5V',16:'+5V',17:'BOOST_FB',18:'BOOST_COMP',19:'BOOST_ILIM',20:'GND',21:'GND'},'Package_DFN_QFN:Texas_VQFN-RHL-20',xy=(58.42,91.44),pcb=(30,46),source=TI+'tps61088.pdf')
box('TLV62569_DBV',[(1,'EN','input'),(2,'GND','power_in'),(3,'SW','power_out'),(4,'VIN','power_in'),(5,'FB','input')])
item('06_main_power','U410','TLV62569_DBV','TLV62569DBVR',{1:'+5V',2:'GND',3:'BUCK3_SW',4:'+5V',5:'BUCK3_FB'},SOT5,xy=(58.42,165.1),pcb=(48,43),source=TI+'tlv62569.pdf')
pas('06_main_power','L400','2.2uH Isat>=12A','SYS_SWITCHED','BOOST_SW',pcb=(26,46),fp='Inductor_SMD:L_Bourns_SRP1038C_10.0x10.0mm')
pas('06_main_power','R400','178k 1%','+5V','BOOST_FB')
pas('06_main_power','R401','56.2k 1%','BOOST_FB','GND')
pas('06_main_power','R402','324k 1%','BOOST_FSW','BOOST_SW')
pas('06_main_power','R403','100k 1%','BOOST_ILIM','GND')
pas('06_main_power','R404','22k loop-tune','BOOST_COMP','BOOST_COMP_RC')
cap('06_main_power','C400','2.2u 10V','BOOST_VCC')
pas('06_main_power','C401','100n','BOOST_BOOT','BOOST_SW')
cap('06_main_power','C402','47n','BOOST_SS')
cap('06_main_power','C403','10n loop-tune','BOOST_COMP_RC')
cap('06_main_power','C404','100p C0G','BOOST_COMP')
for i in range(2):cap('06_main_power','C'+str(405+i),'22u 10V','SYS_SWITCHED',fp='Capacitor_SMD:C_1206_3216Metric')
for i in range(3):cap('06_main_power','C'+str(407+i),'47u 10V','+5V',fp='Capacitor_SMD:C_1210_3225Metric')
pas('06_main_power','L410','2.2uH Isat>=3.5A','BUCK3_SW','+3V3',pcb=(51,43),fp='Inductor_SMD:L_Sunlord_SWPA4020S')
pas('06_main_power','R410','453k 1%','+3V3','BUCK3_FB')
pas('06_main_power','R411','100k 1%','BUCK3_FB','GND')
cap('06_main_power','C410','10u 10V','+5V',fp='Capacitor_SMD:C_0805_2012Metric')
cap('06_main_power','C411','22u 10V','+3V3',fp='Capacitor_SMD:C_0805_2012Metric')
cap('06_main_power','C412','22u 10V','+3V3',fp='Capacitor_SMD:C_0805_2012Metric')
pas('06_main_power','C413','22p C0G','+3V3','BUCK3_FB')
flag('06_main_power',410,'+3V3',(58.42,241.3))

page('07_sd','07 microSD 4-bit interface',(116,59),[
 '3.3 V SDMMC only. No 1.8 V UHS. Set all host pins high-Z BEFORE SD power removal.',
 'Pullups are on switched VSD. Firmware must prevent IO back-power; qualification pending.',
 'Right-edge card opening; allow insertion/ejection stroke outside enclosure wall.'
])
box('TPS22918',[(1,'VIN','power_in'),(2,'GND','power_in'),(3,'ON','input'),(4,'CT','passive'),(5,'QOD','passive'),(6,'VOUT','power_out')])
box('MICRO_SD',[(1,'DAT2','bidirectional'),(2,'DAT3','bidirectional'),(3,'CMD','bidirectional'),(4,'VDD','power_in'),(5,'CLK','input'),(6,'VSS','power_in'),(7,'DAT0','bidirectional'),(8,'DAT1','bidirectional'),(9,'DET','passive'),(10,'DET_COMMON','passive'),('SH','SHIELD','passive')])
item('07_sd','J500','MICRO_SD','microSD DM3AT',{1:'CARD_D2',2:'CARD_D3',3:'CARD_CMD',4:'VSD',5:'CARD_CLK',6:'GND',7:'CARD_D0',8:'CARD_D1',9:'SD_CARD_DETECT',10:'GND','SH':'GND'},'Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5',xy=(69.85,88.9),pcb=(135,55),angle=90)
item('07_sd','U500','TPS22918','TPS22918DBVR',{1:'+3V3',2:'SD_POWER_EN',3:'SD_SLEW',4:'GND',5:'SD_QOD',6:'VSD'},SOT6,xy=(69.85,147.32),pcb=(122,55),source=TI+'tps22918.pdf')
pas('07_sd','R500','100k','SD_POWER_EN','GND');cap('07_sd','C500','1n','SD_SLEW');pas('07_sd','R501','100R','SD_QOD','VSD')
cap('07_sd','C501','1u 10V','+3V3');cap('07_sd','C502','22u 10V','VSD',fp='Capacitor_SMD:C_0805_2012Metric');cap('07_sd','C503','100n','VSD')
for i,signal in enumerate(['D0','D1','D2','D3','CMD','CLK']):
    pas('07_sd','R'+str(510+i),'22R','SD_'+signal,'CARD_'+signal)
    if signal!='CLK':pas('07_sd','R'+str(520+i),'47k','VSD','CARD_'+signal)
pas('07_sd','R530','10k','+3V3','SD_CARD_DETECT');cap('07_sd','C504','10n','SD_CARD_DETECT')

pas('05_system_switch','R313','10k','+3V3','MAIN_PG')
tp('05_system_switch','TP310','MAIN_PG')
cap('03_usb_current','C216','100n','CHARGE_INPUT_EN')

page('08_modem','08 Optional ML307A-GCLN',(98,17),[
 'ML307A-GCLN only; all 94 pads including central grounds are explicit.',
 'UART control is 1.8 V. VDD_EXT is a module OUTPUT, never driven externally.',
 'Optional assembly. RF layout, USB RNDIS firmware and GNSS coexistence need sample validation.'
])
mp={n:None for n in range(1,95)};pn={n:'RSV_'+str(n) for n in range(1,95)};pt={n:'passive' for n in range(1,95)}
for row in csv.DictReader((ROOT/'display_board/hardware/display-carrier/MODEM_PIN_PLAN.csv').open(encoding='utf-8-sig')):
    ids=[int(z) for z in row['ml307_pad'].split(';') if z.isdigit()]
    for n in ids:
        if row['status']=='no_connect':pn[n]='NC_'+str(n);continue
        sig=row['net'];sig={'MODEM_USB_DM':'MODEM_USB_N','MODEM_USB_DP':'MODEM_USB_P','MODEM_POWER_EN':'MODEM_3V8','MODEM_USB_VBUS_EN':'MODEM_VBUS5','MODEM_PWRKEY':'MODEM_KEY_N','MODEM_AT_TX':'MODEM_UART_RX_1V8','MODEM_AT_RX':'MODEM_UART_TX_1V8','MODEM_DTR':'MODEM_DTR_1V8','MODEM_STATE':'MODEM_STATE_1V8','MODEM_GND':'GND','MODEM_VDD_EXT':'MODEM_1V8'}.get(sig,sig)
        mp[n]=sig;pn[n]=row['ml307_signal'] if ';' not in row['ml307_pad'] else sig
        pt[n]='power_in' if sig in ['GND','MODEM_3V8','MODEM_VBUS5'] else 'power_out' if n in [14,24] else 'output' if n in [18,20,22,25,39,49] else 'input' if n in [7,12,13,15,17,19,23,38,79,82] else 'bidirectional'
box('ML307A_GCLN',[(n,pn[n],pt[n]) for n in range(1,95)],25.4)
item('08_modem','U1000','ML307A_GCLN','ML307A-GCLN',mp,'Carrier:ML307A_GCLN_LGA94',xy=(76.2,142.24),pcb=(116,28),angle=90,source='https://openluat.com/ml307a/',status='optional; footprint and interface verification pending')
for i,(outa,ina,inb,outb) in enumerate([('MODEM_AT_RX','MODEM_AT_TX','MODEM_UART_TX_1V8','MODEM_UART_RX_1V8'),('MODEM_STATE','MODEM_DTR','MODEM_STATE_1V8','MODEM_DTR_1V8')]):
    item('08_modem','U'+str(1001+i),'TXU0202_DCU','TXU0202DCUR',{1:inb,2:'GND',3:'+3V3',4:outa,5:ina,6:'+3V3',7:'MODEM_1V8',8:outb},'Package_SO:VSSOP-8_2.3x2mm_P0.5mm',xy=(215.9+i*114.3,60.96),pcb=(96+i*8,39),source=TI+'txu0202.pdf')
cursor['08_modem']=8
cap('08_modem','C1000','100n','MODEM_1V8');cap('08_modem','C1001','1u','MODEM_1V8');cap('08_modem','C1002','100n','+3V3');cap('08_modem','C1003','100n','+3V3')
mos('08_modem','Q1000','MODEM_PWRKEY','GND','MODEM_KEY_N');pas('08_modem','R1000','100k','MODEM_PWRKEY','GND')
pas('08_modem','R1001','100k','MODEM_DTR','GND')
cap('08_modem','C1004','1n','MODEM_RESET_TP')
for i,n in enumerate(['MODEM_RESET_TP','MODEM_BOOT_TP','MODEM_DBG_RX_TP','MODEM_DBG_TX_TP','MODEM_WAKE_TP','MODEM_RI_TP','MODEM_CTS_TP','MODEM_RTS_TP']):tp('08_modem','TP'+str(1000+i),n,pcb=(96+i*2.5,49))

page('09_modem_power','09 Modem power and RF',(96,53),[
 '3.8 V / 2 A burst rail. 440 uF startup and RF burst droop MUST be measured.',
 'USB VBUS detection and active-antenna bias are independently switched.',
 'GNSS/LTE pi networks start at 0R series, shunt capacitors DNP; tune with enclosure/antenna.'
])
box('TLV62585_DRL',[(1,'GND','power_in'),(2,'SW','power_out'),(3,'VIN','power_in'),(4,'EN','input'),(5,'FB','input'),(6,'NC','no_connect')])
item('09_modem_power','U1010','TLV62585_DRL','TLV62585DRLR',{1:'GND',2:'MODEM_SW',3:'+5V',4:'MODEM_POWER_EN',5:'MODEM_FB',6:None},'Package_TO_SOT_SMD:SOT-563',xy=(57.15,81.28),pcb=(110,47),source=TI+'tlv62585.pdf',status='thermal qualification required')
pas('09_modem_power','L1010','1uH Isat>=4.5A','MODEM_SW','MODEM_3V8',pcb=(114,47),fp='Inductor_SMD:L_Sunlord_SWPA4020S')
pas('09_modem_power','R1010','536k 1%','MODEM_3V8','MODEM_FB');pas('09_modem_power','R1011','100k 1%','MODEM_FB','GND');pas('09_modem_power','R1012','100k','MODEM_POWER_EN','GND')
cap('09_modem_power','C1010','22u 10V','+5V',fp='Capacitor_SMD:C_0805_2012Metric');cap('09_modem_power','C1011','22u 10V','MODEM_3V8',fp='Capacitor_SMD:C_0805_2012Metric')
for i in range(2):cap('09_modem_power','C'+str(1012+i),'220u 6.3V low ESR','MODEM_3V8',pcb=(119+i*7,43),fp='Capacitor_Tantalum_SMD:CP_EIA-7343-31_Kemet-D')
cap('09_modem_power','C1014','100n','MODEM_3V8');cap('09_modem_power','C1015','100p C0G','MODEM_3V8');cap('09_modem_power','C1016','33p C0G','MODEM_3V8')
# Discharge node is active only with rail EN low. Gate bias disappears with system power.
mos('09_modem_power','Q1010','MODEM_POWER_EN','GND','MODEM_DISCHARGE_EN');pas('09_modem_power','R1013','47k','+3V3','MODEM_DISCHARGE_EN')
mos('09_modem_power','Q1011','MODEM_DISCHARGE_EN','GND','MODEM_DISCHARGE');pas('09_modem_power','R1014','100R 0.25W','MODEM_3V8','MODEM_DISCHARGE',fp='Resistor_SMD:R_1206_3216Metric')
item('09_modem_power','U1011','TPS22918','TPS22918DBVR',{1:'+5V',2:'MODEM_USB_VBUS_EN',3:'MODEM_USB_SLEW',4:'GND',5:'MODEM_USB_QOD',6:'MODEM_VBUS5'},SOT6,xy=(57.15,139.7),pcb=(94,32),source=TI+'tps22918.pdf')
pas('09_modem_power','R1015','100k','MODEM_USB_VBUS_EN','GND');pas('09_modem_power','R1016','1k','MODEM_USB_QOD','MODEM_VBUS5');cap('09_modem_power','C1017','1n','MODEM_USB_SLEW');cap('09_modem_power','C1018','1u 10V','MODEM_VBUS5')
item('09_modem_power','U1012','AP2112K','AP2112K-3.3TRG1',{1:'+5V',2:'GND',3:'GNSS_ANT_POWER_EN',4:None,5:'GNSS_A3V3'},SOT5,xy=(57.15,198.12),pcb=(100,9))
cap('09_modem_power','C1019','1u','GNSS_A3V3');cap('09_modem_power','C1020','1u','+5V');pas('09_modem_power','R1017','100k','GNSS_ANT_POWER_EN','GND')
# Antenna feed design deliberately remains open rather than risking an unverified RF/DC combination.
tp('09_modem_power','TP1010','GNSS_A3V3');tp('09_modem_power','TP1011','MODEM_3V8')
flag('09_modem_power',1010,'MODEM_3V8',(50.8,251.46))

page('10_sim_rf','10 SIM and antenna interfaces',(124,16),[
 'SIM voltage is supplied by modem. Do not connect 3.3 V to SIM0_VCC.',
 'RF traces require 50 ohm controlled impedance and antenna matching validation.',
 'GNSS connector is DC-blocked. Active-antenna bias is a separate release hold.'
])
box('NANOSIM',[(1,'VCC','power_in'),(2,'RST','input'),(3,'CLK','input'),(5,'GND','power_in'),(6,'VPP_NC','no_connect'),(7,'DATA','bidirectional'),('SH','SHIELD','passive'),('SW','DETECT','passive')])
item('10_sim_rf','J1000','NANOSIM','GCT SIM8060-6-1-14-00',{1:'SIM0_VCC',2:'SIM_RST_SOCKET',3:'SIM_CLK_SOCKET',5:'GND',6:None,7:'SIM_DATA_SOCKET','SH':'GND','SW':'SIM0_DET'},'Connector_Card:nanoSIM_GCT_SIM8060-6-1-14-00',xy=(63.5,73.66),pcb=(136,28),angle=90)
for i,(n,t) in enumerate([('SIM0_DATA','SIM_DATA_SOCKET'),('SIM0_RST','SIM_RST_SOCKET'),('SIM0_CLK','SIM_CLK_SOCKET')]):pas('10_sim_rf','R'+str(1030+i),'22R',n,t)
cap('10_sim_rf','C1030','100n','SIM0_VCC');pas('10_sim_rf','R1033','47k','MODEM_1V8','SIM0_DET');cap('10_sim_rf','C1031','10n','SIM0_DET')
item('10_sim_rf','U1030','USBLC6','USBLC6-2SC6',{1:'SIM_DATA_SOCKET',2:'GND',3:'SIM_CLK_SOCKET',4:'SIM_CLK_SOCKET',5:'SIM0_VCC',6:'SIM_DATA_SOCKET'},SOT6,xy=(63.5,127),pcb=(132,35))
item('10_sim_rf','U1031','USBLC6','USBLC6-2SC6',{1:'SIM_RST_SOCKET',2:'GND',3:'GND',4:'GND',5:'SIM0_VCC',6:'SIM_RST_SOCKET'},SOT6,xy=(63.5,177.8),pcb=(135,37))
box('RF_CONN',[(1,'RF','passive'),(2,'GND','passive')],5.08)
for i,(src,feed) in enumerate([('LTE_RF','LTE_ANT'),('GNSS_RF','GNSS_ANT')]):
    item('10_sim_rf','J'+str(1010+i),'RF_CONN','IPEX-1 50R',{1:feed,2:'GND'},'Connector_Coaxial:U.FL_Hirose_U.FL-R-SMT-1_Vertical',pcb=(112+i*13,8))
    pas('10_sim_rf','R'+str(1040+i),'0R RF match',src,feed)
    for j,net in enumerate([src,feed]):pas('10_sim_rf','C'+str(1040+i*2+j),'DNP RF tune',net,'GND',dnp=True)
# Convert the GNSS series element to an RF DC block, not a zero-ohm antenna feed.
for p in d.parts:
    if p['ref']=='R1041':p['ref']='C1044';p['kind']='C';p['value']='100p C0G RF DC block';p['footprint']=CFP

page('11_codec','11 Codec and remote microphone',(10,61),[
 'ES8311: I2C 0x18 (CE=0), slave I2S. Keep analog input loop away from switch nodes.',
 'Remote headboard: LinkMems LMA3729T421-OA1; 4-wire harness <=100 mm initial target.',
 'DAC outputs go to source selector. NS4814 power stage is held pending a complete datasheet.'
])
box('ES8311',[(1,'CCLK','input'),(2,'MCLK','input'),(3,'PVDD','power_in'),(4,'DVDD','power_in'),(5,'DGND','power_in'),(6,'SCLK','bidirectional'),(7,'ASDOUT','output'),(8,'LRCK','bidirectional'),(9,'DSDIN','input'),(10,'AGND','power_in'),(11,'AVDD','power_in'),(12,'OUTP','output'),(13,'OUTN','output'),(14,'DACVREF','passive'),(15,'ADCVREF','passive'),(16,'VMID','passive'),(17,'MIC1N','input'),(18,'MIC1P','input'),(19,'CDATA','bidirectional'),(20,'CE','input'),(21,'EP_GND','power_in')])
item('11_codec','U800','ES8311','ES8311',{1:'SYSTEM_I2C_SCL',2:'CODEC_MCLK',3:'+3V3',4:'+3V3',5:'GND',6:'CODEC_BCLK',7:'CODEC_DOUT',8:'CODEC_LRCK',9:'CODEC_DIN',10:'GND',11:'AUDIO_3V3',12:'DAC_P',13:'DAC_N',14:'DAC_VREF',15:'ADC_VREF',16:'CODEC_VMID',17:'MIC_N',18:'MIC_P',19:'SYSTEM_I2C_SDA',20:'GND',21:'GND'},'Package_DFN_QFN:QFN-20-1EP_3x3mm_P0.4mm_EP1.65x1.65mm',xy=(64.77,86.36),pcb=(22,65),source='https://files.waveshare.com/wiki/common/ES8311.DS.pdf')
item('11_codec','U801','AP2112K','AP2112K-3.3TRG1',{1:'+5V',2:'GND',3:'+5V',4:None,5:'AUDIO_3V3'},SOT5,xy=(64.77,147.32),pcb=(11,61))
box('CONN4',[(i,str(i),'passive') for i in range(1,5)],5.08)
item('11_codec','J800','CONN4','MIC HEAD / 3V3,P,N,GND',{1:'AUDIO_3V3',2:'MIC_CABLE_P',3:'MIC_CABLE_N',4:'GND'},'Connector_JST:JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical',xy=(64.77,208.28),pcb=(8,68),angle=90)
for i,(a,b) in enumerate([('I2S_MCLK','CODEC_MCLK'),('I2S_BCLK','CODEC_BCLK'),('I2S_LRCK','CODEC_LRCK'),('I2S_DOUT','CODEC_DIN'),('CODEC_DOUT','I2S_DIN')]):pas('11_codec','R'+str(800+i),'22R',a,b)
for i,(v,n) in enumerate([('1u','+5V'),('1u','AUDIO_3V3'),('100n','AUDIO_3V3'),('100n','+3V3'),('1u','+3V3'),('1u','DAC_VREF'),('1u','ADC_VREF'),('1u','CODEC_VMID')]):cap('11_codec','C'+str(800+i),v,n)
pas('11_codec','C808','1u 10V','MIC_CABLE_P','MIC_P');pas('11_codec','C809','1u 10V','MIC_CABLE_N','MIC_N')
pas('11_codec','C810','100p C0G','MIC_P','MIC_N')

page('12_fm_audio','12 FM and source switching',(33,63),[
 'FM uses a complete RDA5807M receiver module with crystal and antenna, not a bare chip.',
 'L/R are summed through separate 10k resistors. FM and DAC outputs are never hard-paralleled.',
 'Switch output is AC-coupled for amplifier bias. AMP stage and speaker power remain release holds.'
])
box('SGM3157',[(1,'NO','passive'),(2,'GND','power_in'),(3,'NC','passive'),(4,'COM','passive'),(5,'VPLUS','power_in'),(6,'SELECT','input')])
box('CONN8',[(i,str(i),'passive') for i in range(1,9)],5.08)
item('12_fm_audio','J810','CONN8','RDA5807M module',{1:'FM_3V3',2:'GND',3:'FM_SDA',4:'FM_SCL',5:'FM_L',6:'FM_R',7:'GND',8:None},'Connector_JST:JST_PH_B8B-PH-K_1x08_P2.00mm_Vertical',xy=(63.5,76.2),pcb=(43,69),angle=90)
item('12_fm_audio','U810','TPS22918','TPS22918DBVR',{1:'AUDIO_3V3',2:'FM_POWER_EN',3:'FM_SLEW',4:'GND',5:'FM_QOD',6:'FM_3V3'},SOT6,xy=(63.5,137.16),pcb=(35,63))
for i,(a,b) in enumerate([('DAC_P','FM_BIASED'),('DAC_N','AUDIO_MID')]):
    item('12_fm_audio','U'+str(811+i),'SGM3157','SGM3157XC6/TR',{1:b,2:'GND',3:a,4:'AMP_AC_'+str(i),5:'AUDIO_3V3',6:'AUDIO_SELECT_LOCAL'},'Package_TO_SOT_SMD:SOT-363_SC-70-6',xy=(63.5,205.74+i*33.02),pcb=(45+i*4,63),source='https://www.sg-micro.com/rect/assets/a22623ad-a59a-4732-b010-4b667f6b9a7c/SGM3157.pdf')
pas('12_fm_audio','R810','100k','FM_POWER_EN','GND');pas('12_fm_audio','R811','100R','FM_QOD','FM_3V3');cap('12_fm_audio','C8101','1n','FM_SLEW');cap('12_fm_audio','C811','1u','FM_3V3');cap('12_fm_audio','C812','1u','AUDIO_3V3')
for i,(lo,hi) in enumerate([('FM_SDA','SYSTEM_I2C_SDA'),('FM_SCL','SYSTEM_I2C_SCL')]):
    mos('12_fm_audio','Q'+str(810+i),'FM_3V3',lo,hi);pas('12_fm_audio','R'+str(812+i),'4.7k','FM_3V3',lo)
pas('12_fm_audio','R814','10k','FM_L','FM_SUM');pas('12_fm_audio','R815','10k','FM_R','FM_SUM')
pas('12_fm_audio','C813','2.2u','FM_SUM','FM_BIASED');pas('12_fm_audio','R816','100k','AUDIO_MID','FM_BIASED')
pas('12_fm_audio','R817','10k','AUDIO_3V3','AUDIO_MID');pas('12_fm_audio','R818','10k','AUDIO_MID','GND');cap('12_fm_audio','C814','10u 10V','AUDIO_MID',fp='Capacitor_SMD:C_0805_2012Metric')
pas('12_fm_audio','R819','1k','AUDIO_SOURCE_SELECT','AUDIO_SELECT_LOCAL');pas('12_fm_audio','R820','100k','AUDIO_SELECT_LOCAL','GND')
cap('12_fm_audio','C815','100n','AUDIO_3V3');cap('12_fm_audio','C816','100n','AUDIO_3V3')
pas('12_fm_audio','C817','2.2u','AMP_AC_0','AMP_INPUT_P');pas('12_fm_audio','C818','2.2u','AMP_AC_1','AMP_INPUT_N')
tp('12_fm_audio','TP810','AMP_INPUT_P');tp('12_fm_audio','TP811','AMP_INPUT_N')

# A visible hold sheet prevents an interface reservation being mistaken for a populated circuit.
page('13_release_holds','13 Pending interface closure',(0,0),[
 'NOT IMPLEMENTED: LCD bias/backlight, final LCD/touch sockets, camera support and NS4814 amplifier.',
 'LCD: vendor 2-lane initialization / legal timing required; FPC contact side and folded tip Y unmeasured.',
 'CAMERA: SC2336 connector pinout known; sequencing, 24MHz source and connector match remain to close.',
 'GNSS: passive RF path drawn. Active antenna current-limit/bias feed is not implemented.',
 'BATTERY: hardware 0..45C cutoff, reverse connection, exact pack/NTC/fuse still need closure.',
 'PCB reservations are KEEP-FREE planning areas, not dummy sockets or finished circuits.',
 'Do not fabricate or call this a commercial-ready design. See DESIGN_STATUS.md for next gates.'
])
# Correct modem-driven SIM output types, and make optional assembly explicit.
for sym in d.symbols['ML307A_GCLN']['tree']:
    if isinstance(sym,list) and sym and str(sym[0])=='symbol':
        from cad import sexp
        for pin in sexp.find_all(sym,'pin'):
            if sexp.atoms(sexp.find(pin,'number'))[0] in ['12','13']:pin[1]='output'
for p in d.parts:
    if p['kind']=='TPS22918':
        old=p['nets'].copy()
        p['nets'].update({'2':old['4'],'3':old['2'],'4':old['3']})
    if p['sheet'] in ['08_modem','09_modem_power','10_sim_rf'] and not p['ref'].startswith('#'):
        p['dnp']=True

if __name__=='__main__':
    d.write();print('Generated',len(d.parts),'parts in',len(d.sheets),'sheets:',OUT)
