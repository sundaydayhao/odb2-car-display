package com.vehicle.capture;

import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;

/** Reads bounded metadata off the UI thread; a sealed file can still be interrupted. */
final class RecordSummary {
    final File file;
    long date, frames, notifications;
    int seconds;
    boolean monitor, complete, readable;
    String adapter="未知适配器", reason="缺少结束标记", protocol="", filter="";
    RecordSummary(File file) { this.file=file; date=file.lastModified(); }
    static RecordSummary read(File file) {
        RecordSummary r=new RecordSummary(file);
        try(RandomAccessFile in=new RandomAccessFile(file,"r")) {
            byte[] prefix=new byte[(int)Math.min(in.length(),16384)]; in.readFully(prefix);
            String text=new String(prefix,StandardCharsets.UTF_8);
            int newline=text.indexOf('\n');
            if(newline<0) return r;
            JSONObject header=new JSONObject(text.substring(0,newline));
            if(!"vehicle-capture/1".equals(header.optString("schema"))) return r;
            r.readable=true; r.date=header.optLong("utc_ms",r.date);
            r.monitor="can_monitor".equals(header.optString("mode"));
            r.adapter=header.optString("adapter",r.adapter); r.seconds=header.optInt("requested_seconds");
            r.protocol=header.optString("protocol"); r.filter=header.optString("filter");
            long offset=Math.max(0,in.length()-16384); in.seek(offset);
            byte[] suffix=new byte[(int)(in.length()-offset)]; in.readFully(suffix);
            String[] lines=new String(suffix,StandardCharsets.UTF_8).split("\n");
            for(int i=lines.length-1;i>=0;i--) {
                if(lines[i].trim().isEmpty()) continue;
                JSONObject end=new JSONObject(lines[i]);
                if("end".equals(end.optString("kind"))) {
                    r.complete=end.optBoolean("transport_complete"); r.frames=end.optLong("frames");
                    r.notifications=end.optLong("notifications"); r.reason=end.optString("reason");
                }
                break;
            }
        } catch(Exception ignored) { /* Preserve partial/corrupt files for user-directed export. */ }
        return r;
    }
    String title() { return !readable?"待检查记录":monitor?"CAN 报文监听":"标准参数记录"; }
    String reasonText() {
        if("duration".equals(reason)) return "已到设定时长";
        if("user".equals(reason)) return "手动停止";
        return reason;
    }
}
