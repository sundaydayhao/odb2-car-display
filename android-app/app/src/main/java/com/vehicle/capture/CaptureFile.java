package com.vehicle.capture;

import android.os.SystemClock;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.UUID;

/** Durable bounded append-only capture. .part files remain exportable after a crash. */
final class CaptureFile {
    static final long MAX_BYTES=16L*1024*1024;
    private final File partial, sealed;
    private final FileOutputStream stream;
    private long bytes, sequence, flushed, transaction, notifications;
    long frames, unparsed;
    private boolean closed;
    CaptureFile(File directory,boolean monitor,int protocol,String filter,int seconds,String adapter,JSONObject bluetooth,boolean physical) throws IOException {
        if(!directory.isDirectory() && !directory.mkdirs()) throw new IOException("无法创建记录目录");
        File[] existing=directory.listFiles(); long total=0;
        if(existing!=null) for(File f:existing) total+=f.length();
        if(existing!=null && (existing.length>=64 || total>=128L*1024*1024)) throw new IOException("记录空间已满，请先导出并删除旧记录");
        String name="capture-"+System.currentTimeMillis()+"-"+UUID.randomUUID().toString().substring(0,8);
        partial=new File(directory,name+".part"); sealed=new File(directory,name+".jsonl");
        stream=new FileOutputStream(partial);
        try {
            JSONObject h=row("header",SystemClock.elapsedRealtimeNanos()/1000);
            h.put("schema","vehicle-capture/1").put("mode",monitor?"can_monitor":"diagnostic")
             .put("adapter",adapter).put("protocol",monitor?String.valueOf(protocol):"auto_can")
             .put("filter",monitor?filter:"").put("requested_seconds",seconds)
             .put("utc_ms",System.currentTimeMillis()).put("timestamp_source","android_ble_delivery")
             .put("loss_detection","adapter_and_app_only").put("app_version",BuildConfig.VERSION_NAME)
             .put("bluetooth",bluetooth);
            h.put("diagnostic_addressing",monitor?"not_applicable":physical?"physical_11bit":"functional");
            append(h); sync();
        } catch(Exception e) { stream.close(); throw new IOException(e); }
    }
    private JSONObject row(String kind,long us) throws Exception {
        return new JSONObject().put("kind",kind).put("sequence",sequence++).put("monotonic_us",us).put("transaction",transaction);
    }
    private void append(JSONObject row) throws IOException {
        byte[] value=(row.toString()+"\n").getBytes(StandardCharsets.UTF_8);
        if(bytes+value.length>MAX_BYTES-2048) throw new IOException("达到单次记录容量上限");
        stream.write(value); bytes+=value.length;
        if(SystemClock.elapsedRealtime()-flushed>=2000) sync();
    }
    private void sync() throws IOException { stream.flush(); stream.getFD().sync(); flushed=SystemClock.elapsedRealtime(); }
    void event(String kind,String value,long us) throws IOException {
        try {
            if(kind.equals("tx")) ++transaction;
            if(kind.equals("unparsed") || kind.equals("partial_line")) ++unparsed;
            append(row(kind,us).put("value",value));
        } catch(Exception e) { throw new IOException(e.getMessage(),e); }
    }
    void raw(byte[] value,long us) throws IOException {
        try { append(row("rx",us).put("notification_id",++notifications).put("hex",CanFrame.hex(value))); }
        catch(Exception e) { throw new IOException(e.getMessage(),e); }
    }
    void frame(CanFrame value,long us) throws IOException {
        try {
            append(row("frame",us).put("id",value.idHex()).put("extended",value.extended)
                .put("dlc",value.data.length).put("data",CanFrame.hex(value.data)));
            ++frames;
        } catch(Exception e) { throw new IOException(e.getMessage(),e); }
    }
    File finish(String reason,boolean complete) throws IOException {
        if(closed) return partial;
        closed=true;
        try {
            JSONObject end=row("end",SystemClock.elapsedRealtimeNanos()/1000).put("reason",reason)
                .put("transport_complete",complete).put("frames",frames).put("unparsed",unparsed)
                .put("bus_loss_unknown",true).put("notifications",notifications);
            byte[] footer=(end.toString()+"\n").getBytes(StandardCharsets.UTF_8);
            stream.write(footer); sync();
        } catch(Exception e) { throw new IOException(e.getMessage(),e); }
        finally { stream.close(); }
        if(!partial.renameTo(sealed)) throw new IOException("封存失败，原始片段仍保留");
        return sealed;
    }
}
