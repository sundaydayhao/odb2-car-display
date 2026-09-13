package com.vehicle.capture;

import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Session handshake for the identified Yuming B02-Z adapter; never an ECU command. */
final class YmObdHandshake {
    private YmObdHandshake() {}
    static String command(String version) {
        String normalized=version.toUpperCase(Locale.ROOT).replace("\r","");
        if(!normalized.contains("CRYPT:")) return null;
        if(!normalized.contains("SHENZHEN YUMING ELECTRONICS CO., LTD.") ||
           !Pattern.compile("(?m)^DEVICE ?TYPE:\\s*B02-Z\\s*$").matcher(normalized).find())
            throw new IllegalArgumentException("此适配器需要尚未支持的厂家握手，原始版本信息已保留");
        Matcher challenge=Pattern.compile("(?m)^CRYPT:([0-9A-F]{8})$").matcher(normalized);
        if(!challenge.find()) throw new IllegalArgumentException("B02-Z 握手挑战格式无效");
        int value=(int)Long.parseLong(challenge.group(1),16);
        if(challenge.find()) throw new IllegalArgumentException("B02-Z 返回了重复握手挑战");
        return String.format(Locale.ROOT,"AT+SETCRYPT%08X",response(value));
    }
    static int response(int challenge) {
        final int key=0x263D9A7E;
        int a=challenge&255,b=(challenge>>>8)&255,c=(challenge>>>16)&255,d=(challenge>>>24)&255;
        int low=(c>>(a/50))|((b<<2)^(key<<(d/12)));
        int next=(a>>(b/63))|((key<<(d/11))^(c>>1));
        int third=(a>>(b/46))|((key<<(a/34))^d);
        int high=(b<<(d/35))|((key<<(a/49))&(key>>18));
        return ((high&255)<<24)|((third&255)<<16)|((next&255)<<8)|(low&255);
    }
}
