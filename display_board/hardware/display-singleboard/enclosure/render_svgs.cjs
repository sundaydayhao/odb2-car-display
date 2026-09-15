const path=require('path');
const sharp=require(process.env.SHARP_MODULE || 'sharp');
const root=__dirname;
(async()=>{
 for(const [src,dst,width] of [
  ['models/assembly-section.svg','models/assembly-section.png',1800],
  ['microphone-carrier/preview/microphone_carrier.svg','microphone-carrier/preview/schematic.png',1700],
  ['microphone-carrier/preview/pcb.svg','microphone-carrier/preview/pcb.png',1000]
 ]) await sharp(path.join(root,src),{density:220}).resize({width}).png().toFile(path.join(root,dst));
})();
