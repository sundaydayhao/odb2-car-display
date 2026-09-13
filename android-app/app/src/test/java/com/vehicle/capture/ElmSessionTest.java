package com.vehicle.capture;

import org.junit.Test;
import static org.junit.Assert.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class ElmSessionTest {
    static final class Harness implements ElmSession.Sink {
        ElmSession session; long now=1000; String last="", reason; boolean complete;
        final List<String> writes=new ArrayList<>(),events=new ArrayList<>();
        final List<CanFrame> frames=new ArrayList<>();
        Harness(boolean monitor) { this(monitor,6,""); }
        Harness(boolean monitor,int protocol,String filter) {
            this(monitor,protocol,filter,false);
        }
        Harness(boolean monitor,int protocol,String filter,boolean physical) {
            session=new ElmSession(this,monitor,protocol,filter,2,physical); session.start(now);
        }
        public boolean write(byte[] bytes) { last=new String(bytes,StandardCharsets.US_ASCII).trim(); writes.add(last); return true; }
        public void event(String kind,String value) { events.add(kind+":"+value); }
        public void frame(CanFrame frame) { frames.add(frame); }
        public void done(String reason,boolean complete) { this.reason=reason;this.complete=complete; }
        void bytes(String value) { session.bytes(value.getBytes(StandardCharsets.US_ASCII),now); }
        void answer(String value,boolean promptFirst) {
            if(promptFirst) { bytes(value+"\r>");session.writeComplete(true,now); }
            else {session.writeComplete(true,now);bytes(value+"\r>");}
            now+=100;session.tick(now);
        }
        void initialized(boolean monitor) {
            int guard=0;
            while(!last.equals(monitor?"ATMA":"010D") && !session.isFinished()) {
                assertTrue(++guard<30);
                answer(last.equals("ATI")||last.equals("ATZ")?"ELM327 v2.2":last.equals("0100")?"7E8 06 41 00 08 18 00 00":last.equals("ATDPN")?"A6":"OK",guard%2==0);
            }
            assertFalse(reason,session.isFinished());
        }
    }
    @Test public void frame11And29Formats() {
        assertEquals("7E8",CanFrame.parse("7E8 3 02 41 0D").idHex());
        assertEquals("18DAF110",CanFrame.parse("18 DA F1 10 2 01 FF").idHex());
        assertTrue(CanFrame.parse("18DAF110 0").extended);
        assertEquals(0,CanFrame.parse("7E8 0").data.length);
    }
    @Test public void malformedFramesRemainUnparsed() {
        for(String line:new String[]{"7E8 8 01 02","FFF 0","FFFFFFFF 0","18 DA F1 ZZ 0","7E8 9 00","7E8 1 GG","7E8 0 11","BUFFER FULL","7E8 RTR","7E8 2 0 00"}) assertNull(line,CanFrame.parse(line));
    }
    @Test public void byteFragmentationAndMultipleFrames() {
        Harness h=new Harness(true);h.initialized(true);h.session.writeComplete(true,h.now);
        String rows="7E8 3 02 41 0D\r18 DA F1 10 2 01 FF\r";
        for(char c:rows.toCharArray()) h.bytes(String.valueOf(c));
        assertEquals(2,h.frames.size()); assertFalse(h.session.isFinished());
    }
    @Test public void allFragmentBoundaries() {
        String input="7E8 3 02 41 0D\r7E9 0\r";
        for(int split=0;split<=input.length();split++) {
            Harness h=new Harness(true);h.initialized(true);h.session.writeComplete(true,h.now);
            h.bytes(input.substring(0,split));h.bytes(input.substring(split));assertEquals(2,h.frames.size());
        }
    }
    @Test public void silentMonitorHasNoVehicleRequests() {
        Harness h=new Harness(true,7,"18DAF110");h.initialized(true);
        assertTrue(h.writes.contains("ATCSM1"));assertTrue(h.writes.contains("ATCFC0"));
        assertTrue(h.writes.contains("ATCAF0"));assertTrue(h.writes.contains("ATCRA18DAF110"));
        for(String value:h.writes) assertTrue(value.startsWith("AT"));
    }
    @Test public void adapterIdentityRequired() {
        Harness h=new Harness(true);h.answer("OK",false);h.answer("unrelated UART",false);
        assertTrue(h.session.isFinished());assertFalse(h.complete);
    }
    @Test public void unsupportedSetupRefused() {
        Harness h=new Harness(true);h.answer("ELM327",false);h.answer("ELM327",false);h.answer("?",false);
        assertTrue(h.session.isFinished());assertFalse(h.complete);
    }
    @Test public void promptDoesNotAdvanceBeforeWriteAck() {
        Harness h=new Harness(true);h.bytes("ELM327\r>");h.now+=100;h.session.tick(h.now);
        assertEquals(1,h.writes.size());h.session.writeComplete(true,h.now);h.now+=100;h.session.tick(h.now);assertEquals("ATI",h.last);
    }
    @Test public void durationStopWaitsForPromptAndDoesNotRestart() {
        Harness h=new Harness(true);h.initialized(true);h.session.writeComplete(true,h.now);
        h.now+=2200;h.session.tick(h.now);assertEquals("",h.last);int count=h.writes.size();
        h.session.writeComplete(true,h.now);h.now+=300;h.session.tick(h.now);assertEquals(count,h.writes.size());
        h.bytes("STOPPED\r>");assertTrue(h.complete);assertEquals("duration",h.reason);
    }
    @Test public void stopTimeoutIsIncomplete() {
        Harness h=new Harness(true);h.initialized(true);h.session.writeComplete(true,h.now);
        h.session.stop("user",h.now);h.session.writeComplete(true,h.now);h.session.tick(h.now+3000);
        assertTrue(h.session.isFinished());assertFalse(h.complete);
    }
    @Test public void captureErrorsAreNotSuccessful() {
        for(String error:new String[]{"BUFFER FULL","CAN ERROR","RX ERROR"}) {
            Harness h=new Harness(true);h.initialized(true);h.session.writeComplete(true,h.now);h.bytes(error+"\r>");
            assertTrue(h.session.isFinished());assertFalse(h.complete);
        }
    }
    @Test public void diagnosticUsesSupportedReadOnlyPids() {
        Harness h=new Harness(false);h.initialized(false);h.answer("7E8 03 41 0D 20",false);
        assertEquals("010C",h.last);h.answer("7E8 04 41 0C 10 20",false);assertEquals("0105",h.last);
        assertFalse(h.writes.contains("ATMA"));assertFalse(h.writes.contains("04"));
    }
    @Test public void oversizedLineAndFailedWriteAbort() {
        Harness h=new Harness(true);h.initialized(true);h.session.writeComplete(true,h.now);h.bytes("A".repeat(1025));assertFalse(h.complete);assertTrue(h.session.isFinished());
        h=new Harness(true);h.session.writeComplete(false,h.now);assertFalse(h.complete);assertTrue(h.session.isFinished());
    }
    @Test public void cancellationBeforeCaptureIsIncomplete() {
        Harness h=new Harness(true);h.session.stop("user",h.now);h.session.writeComplete(true,h.now);
        h.session.writeComplete(true,h.now);h.bytes(">");assertFalse(h.complete);assertTrue(h.session.isFinished());
    }
    @Test public void invalidFilterRejected() {
        for(String bad:new String[]{"FFF","18DAF110","7E8\rATZ","xyz"}) {
            try {new Harness(true,6,bad);fail(bad);}catch(IllegalArgumentException expected){}
        }
    }
    @Test public void diagnosticStopDoesNotMislabelIsoTpLengthAsCanDlc() {
        Harness h=new Harness(false);h.initialized(false);h.session.writeComplete(true,h.now);
        h.session.stop("user",h.now);h.session.writeComplete(true,h.now);
        h.bytes("7E8 03 41 0D 20\r>");
        assertTrue(h.session.isFinished());assertEquals(0,h.frames.size());
    }
    @Test public void diagnosticIgnoresDisabledCanFilter() {
        Harness h=new Harness(false,6,"18DAF110");h.initialized(false);
        assertEquals("010D",h.last);
    }
    @Test public void physicalDiagnosticUsesOnlyDiscoveredEcu() {
        Harness h=new Harness(false,6,"",true);h.initialized(false);
        assertTrue(h.writes.contains("ATSH7E0"));assertTrue(h.writes.contains("ATCRA7E8"));
        assertEquals("010D",h.last);assertFalse(h.writes.contains("ATMA"));
        assertTrue(h.writes.indexOf("ATDPN")<h.writes.indexOf("ATSH7E0"));
    }
    @Test public void physicalDiagnosticRefusesMissingHeader() {
        Harness h=new Harness(false,6,"",true);
        while(!h.last.equals("0100")) h.answer(h.last.equals("ATI")||h.last.equals("ATZ")?"ELM327":"OK",false);
        h.answer("41 00 08 18 00 00",false);
        assertTrue(h.session.isFinished());assertFalse(h.complete);
        assertFalse(h.writes.contains("ATSH7E0"));
    }
    @Test public void monitorDoesNotEnablePhysicalDiagnostic() {
        Harness h=new Harness(true,6,"",true);h.initialized(true);
        for(String value:h.writes) assertTrue(value.startsWith("AT"));
        assertFalse(h.writes.contains("ATSH7E0"));
    }
    @Test public void manufacturerHandshakeCompletesBeforeAnyVehicleRequest() {
        Harness h=new Harness(false);
        while(!h.last.equals("AT+VERSION")) h.answer(h.last.equals("ATI")||h.last.equals("ATZ")?"ELM327":"OK",false);
        h.answer("SHENZHEN YUMING ELECTRONICS CO., LTD.\nDEVICE TYPE:B02-Z\nCRYPT:6997EB7C",false);
        assertEquals("AT+SETCRYPTD89B4FAD",h.last);assertFalse(h.writes.contains("0100"));
        h.answer("?",false);assertTrue(h.session.isFinished());assertFalse(h.writes.contains("0100"));
    }
}
