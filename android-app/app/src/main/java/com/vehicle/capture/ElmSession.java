package com.vehicle.capture;

import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Single-owner ELM state machine. All calls are serialized by the service worker. */
public final class ElmSession {
    public interface Sink {
        boolean write(byte[] value);
        void event(String kind, String value);
        void frame(CanFrame frame);
        void done(String reason, boolean complete);
    }
    private final Sink sink;
    private final boolean monitor;
    private final boolean physical;
    private int targetEcu;
    private final int seconds;
    private final ArrayDeque<String> setup=new ArrayDeque<>();
    private final ArrayList<String> pids=new ArrayList<>();
    private final StringBuilder line=new StringBuilder(), reply=new StringBuilder();
    private String command="", stopReason;
    private long now, deadline, captureUntil;
    private boolean writing, prompted, finished, capturing, stopping;
    private int poll;
    public ElmSession(Sink sink, boolean monitor, int protocol, String filter, int seconds) {
        this(sink,monitor,protocol,filter,seconds,false);
    }
    public ElmSession(Sink sink, boolean monitor, int protocol, String filter, int seconds, boolean physical) {
        if(seconds<1 || seconds>600 || protocol<6 || protocol>9) throw new IllegalArgumentException("采集参数无效");
        this.sink=sink; this.monitor=monitor; this.seconds=seconds;
        this.physical=physical && !monitor;
        filter=monitor?filter.trim().toUpperCase(Locale.ROOT):"";
        if(!filter.isEmpty() && (!filter.matches(protocol==6||protocol==8?"[0-9A-F]{3}":"[0-9A-F]{8}") ||
            Long.parseLong(filter,16)>(protocol==6||protocol==8?0x7ffL:0x1fffffffL))) throw new IllegalArgumentException("过滤地址与协议不匹配");
        for(String s:new String[]{"ATZ","ATI","ATE0","ATL0","ATS1","ATH1","AT+VERSION"}) setup.add(s);
        if(monitor) {
            for(String s:new String[]{"ATSP"+protocol,"ATCSM1","ATCFC0","ATCAF0","ATD1",filter.isEmpty()?"ATCRA":"ATCRA"+filter}) setup.add(s);
        } else {
            for(String s:new String[]{"ATCAF1","ATD0","ATCFC1","ATAT1","ATST64","ATSP0","0100","ATDPN"}) setup.add(s);
        }
    }
    public void start(long nowMs) { now=nowMs; advance(); }
    public boolean isFinished() { return finished; }
    private void send(String value, int timeout) {
        command=value; reply.setLength(0); prompted=false; writing=true; deadline=now+timeout;
        sink.event("tx",value);
        if(finished) return;
        if(!sink.write((value+"\r").getBytes(StandardCharsets.US_ASCII))) fail("蓝牙写入未提交");
    }
    public void writeComplete(boolean success, long nowMs) {
        if(finished) return;
        now=nowMs; writing=false;
        if(!success) { fail("蓝牙写入失败"); return; }
        if(stopReason!=null && !stopping) { sendStop(); return; }
        progress();
    }
    public void bytes(byte[] bytes, long nowMs) {
        if(finished) return;
        now=nowMs;
        for(byte raw:bytes) {
            if(finished) break;
            int c=raw&255;
            if(c=='>') { flushLine(); prompted=true; progress(); }
            else if(c=='\r' || c=='\n') flushLine();
            else if(c>=32 && c<=126) {
                if(line.length()>=1024) { fail("适配器单行过长，记录已中断"); break; }
                line.append((char)c);
            } else if(c!=0) { fail("收到非文本数据，适配器可能不兼容"); break; }
        }
    }
    private void flushLine() {
        if(line.length()==0) return;
        String value=line.toString().trim().toUpperCase(Locale.ROOT); line.setLength(0);
        if(value.isEmpty()) return;
        sink.event("line",value);
        if(value.contains("BUFFER FULL") || value.contains("CAN ERROR") || value.contains("BUS ERROR") || value.contains("RX ERROR")) {
            fail("适配器报告错误或丢包: "+value); return;
        }
        if(monitor && (command.equals("ATMA") || stopping)) {
            CanFrame frame=CanFrame.parse(value);
            if(frame!=null) sink.frame(frame);
            else if(!value.equals("STOPPED") && !value.equals("ATMA")) sink.event("unparsed",value);
        } else {
            if(reply.length()+value.length()>8192) { fail("响应超出容量"); return; }
            reply.append(value).append('\n');
        }
    }
    private void progress() {
        if(finished || writing) return;
        if(command.equals("ATMA") && !stopping) {
            if(prompted) fail("监听提前结束或不受支持");
            return;
        }
        if(!prompted) return;
        if(stopping) { finish(stopReason,true); return; }
        String response=reply.toString();
        sink.event("response",response);
        if(command.equals("ATI")) {
            if(!response.contains("ELM327") && !response.contains("STN")) { fail("设备未识别为 ELM327/STN"); return; }
        } else if(command.equals("AT+VERSION")) {
            // Optional manufacturer identification; ordinary ELM devices may reply '?'.
            try {
                String handshake=YmObdHandshake.command(response);
                if(handshake!=null) setup.addFirst(handshake);
            } catch(IllegalArgumentException e) { fail(e.getMessage()); return; }
        } else if(command.equals("ATDPN")) {
            if(!response.trim().matches("A?[6789]")) { fail("首版仅支持标准 CAN 诊断协议"); return; }
            if(physical) {
                if(!response.trim().matches("A?[68]") || targetEcu<0x7e8 || targetEcu>0x7ef) { fail("定向读取目前仅支持已发现 ECU 的 11 位 CAN"); return; }
                setup.add(String.format(Locale.ROOT,"ATSH%03X",targetEcu-8));
                setup.add(String.format(Locale.ROOT,"ATCRA%03X",targetEcu));
            }
        } else if(command.startsWith("AT") && !command.equals("ATZ") && !response.matches("(?s).*\\bOK\\b.*")) {
            fail("适配器不支持指令: "+command); return;
        } else if(command.equals("0100") && !capturing) {
            if(physical) {
                Matcher ecu=Pattern.compile("(?m)^\\s*(7E[89A-F])\\s+(?:[1-8]\\s+)?06\\s+(41 00 (?:[0-9A-F]{2} ){3}[0-9A-F]{2})(?:\\s|$)").matcher(response);
                if(!ecu.find()) { fail("未发现可定向读取的 11 位 ECU，原始响应已保留"); return; }
                targetEcu=Integer.parseInt(ecu.group(1),16); response=ecu.group(2);
            }
            Matcher m=Pattern.compile("(?:^|\\s)41 00 ([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2})").matcher(response);
            if(!m.find()) { fail("未读到车辆参数能力，请检查车辆上电状态"); return; }
            long bits=Long.parseLong(m.group(1)+m.group(2)+m.group(3)+m.group(4),16);
            for(int pid:new int[]{0x0D,0x0C,0x05}) if((bits&(1L<<(32-pid)))!=0) pids.add(String.format(Locale.ROOT,"01%02X",pid));
            if(pids.isEmpty()) { fail("车辆未声明支持首版诊断参数"); return; }
        }
        /* Wait until the next timer tick, so trailing bytes cannot enter a new transaction. */
        command=""; prompted=false; deadline=now+50;
    }
    private void advance() {
        if(!setup.isEmpty()) { String next=setup.remove(); send(next,next.equals("0100")?22000:5000); return; }
        if(!capturing) {
            capturing=true; captureUntil=now+seconds*1000L;
            sink.event("capture_started",monitor?"can_monitor":"diagnostic");
            if(monitor) { send("ATMA",seconds*1000+3000); return; }
        }
        if(!monitor) send(pids.get(poll++%pids.size()),3000);
    }
    public void tick(long nowMs) {
        if(finished) return;
        now=nowMs;
        if(capturing && now>=captureUntil && stopReason==null) stop("duration",now);
        if(finished) return;
        if(stopping) { if(now>=deadline) finish("停止确认超时: "+stopReason,false); return; }
        if(command.isEmpty() && now>=deadline) { advance(); return; }
        if(!command.isEmpty() && now>=deadline && !stopping) fail("响应或写入超时: "+command);
    }
    public void stop(String reason,long nowMs) {
        if(finished || stopReason!=null) return;
        now=nowMs; stopReason=reason;
        if(!writing) sendStop();
        else deadline=Math.min(deadline,now+2000);
    }
    private void sendStop() {
        stopping=true; send("",2000);
    }
    public void fail(String reason) { finish(reason,false); }
    private void finish(String reason,boolean complete) {
        if(finished) return;
        finished=true;
        if(line.length()>0) { sink.event("partial_line",line.toString()); complete=false; }
        sink.done(reason,complete && capturing);
    }
}
