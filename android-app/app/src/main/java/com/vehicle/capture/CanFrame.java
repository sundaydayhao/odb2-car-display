package com.vehicle.capture;

import java.util.Locale;

/** Raw classical CAN frames with ATH1/ATS1/ATD1/ATCAF0; no synthetic timestamps. */
public final class CanFrame {
    public final long id;
    public final boolean extended;
    public final byte[] data;
    private CanFrame(long id, boolean extended, byte[] data) { this.id=id; this.extended=extended; this.data=data; }
    public static CanFrame parse(String line) {
        String[] t=line.trim().toUpperCase(Locale.ROOT).split("\\s+");
        try {
            int pos; String id;
            if(t.length>=2 && (t[0].length()==3 || t[0].length()==8)) { id=t[0]; pos=1; }
            else if(t.length>=5 && t[0].length()==2 && t[1].length()==2 && t[2].length()==2 && t[3].length()==2) {
                id=t[0]+t[1]+t[2]+t[3]; pos=4;
            } else return null;
            if(!id.matches("[0-9A-F]+") || !t[pos].matches("[0-8]")) return null;
            long address=Long.parseLong(id,16);
            if(address>(id.length()==3 ? 0x7ffL : 0x1fffffffL)) return null;
            int dlc=Integer.parseInt(t[pos++],16);
            if(t.length!=pos+dlc) return null;
            byte[] data=new byte[dlc];
            for(int i=0;i<dlc;i++) { if(!t[pos+i].matches("[0-9A-F]{2}")) return null; data[i]=(byte)Integer.parseInt(t[pos+i],16); }
            return new CanFrame(address,id.length()==8,data);
        } catch(NumberFormatException | IndexOutOfBoundsException e) { return null; }
    }
    public String idHex() { return String.format(Locale.ROOT,extended?"%08X":"%03X",id); }
    public static String hex(byte[] bytes) {
        final String alphabet="0123456789ABCDEF";
        char[] output=new char[bytes.length*2];
        for(int i=0;i<bytes.length;i++) {
            int value=bytes[i]&255;
            output[2*i]=alphabet.charAt(value>>>4); output[2*i+1]=alphabet.charAt(value&15);
        }
        return new String(output);
    }
}
