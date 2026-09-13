package com.vehicle.capture;

import java.util.Locale;

/** User-facing configuration; validation matches the ELM session's accepted filters. */
final class CaptureOptions {
    static final String[] MODES={"标准参数记录","CAN 报文监听"};
    static final String[] DURATIONS={"30 秒","1 分钟","3 分钟","5 分钟"};
    static final int[] SECONDS={30,60,180,300};
    static final String[] PROTOCOLS={"11 位 · 500 kbit/s","29 位 · 500 kbit/s","11 位 · 250 kbit/s","29 位 · 250 kbit/s"};
    static final String[] THEMES={"跟随系统","浅色","深色"};
    static int index(int value,int count,int fallback) { return value>=0 && value<count?value:fallback; }
    static String filter(String value,int protocol) {
        if(protocol<6 || protocol>9) throw new IllegalArgumentException("请选择有效的 CAN 协议");
        String normalized=value.trim().toUpperCase(Locale.ROOT);
        if(normalized.isEmpty()) return "";
        boolean standard=protocol==6 || protocol==8;
        if(!normalized.matches(standard?"[0-9A-F]{3}":"[0-9A-F]{8}") ||
            Long.parseLong(normalized,16)>(standard?0x7ffL:0x1fffffffL))
            throw new IllegalArgumentException(standard?"请输入 000–7FF 范围内的 3 位 CAN ID，或留空接收全部":"请输入 00000000–1FFFFFFF 范围内的 8 位 CAN ID，或留空接收全部");
        return normalized;
    }
}
