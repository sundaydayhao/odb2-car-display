package com.vehicle.capture;

import org.junit.Test;
import org.junit.Rule;
import org.junit.rules.TemporaryFolder;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import static org.junit.Assert.*;

public class RecordSummaryTest {
    @Rule public TemporaryFolder directory=new TemporaryFolder();
    private final String header="{\"kind\":\"header\",\"schema\":\"vehicle-capture/1\",\"mode\":\"diagnostic\",\"utc_ms\":123456789,\"requested_seconds\":60,\"adapter\":\"Test adapter\"}\n";
    private File record(String name,String text) throws Exception {File f=directory.newFile(name);Files.write(f.toPath(),text.getBytes(StandardCharsets.UTF_8));return f;}
    @Test public void sealedInterruptedSessionIsNotMarkedComplete() throws Exception {
        RecordSummary r=RecordSummary.read(record("interrupted.jsonl",header+"{\"kind\":\"end\",\"transport_complete\":false,\"reason\":\"link lost\",\"notifications\":21}\n"));
        assertTrue(r.readable);assertFalse(r.complete);assertEquals("link lost",r.reasonText());assertEquals(21,r.notifications);
    }
    @Test public void completedSessionHasActualDateAndCounts() throws Exception {
        RecordSummary r=RecordSummary.read(record("normal.jsonl",header+"{\"kind\":\"end\",\"transport_complete\":true,\"reason\":\"duration\",\"notifications\":151}\n"));
        assertTrue(r.complete);assertFalse(r.monitor);assertEquals(123456789,r.date);assertEquals(151,r.notifications);assertEquals(60,r.seconds);
    }
    @Test public void truncatedAndForeignFilesRemainExportableWithNoSuccessClaim() throws Exception {
        for(String text:new String[]{header+"{\"kind\":\"rx\",", "not json", "{\"schema\":\"unknown/2\"}\n"}){
            RecordSummary r=RecordSummary.read(record(java.util.UUID.randomUUID()+".part",text));assertFalse(r.complete);assertTrue(r.file.exists());
        }
    }
    @Test public void metadataReadDoesNotNeedToLoadWholeCapture() throws Exception {
        File f=directory.newFile("large.jsonl");try(OutputStream out=new FileOutputStream(f)){out.write(header.getBytes(StandardCharsets.UTF_8));byte[] block=("{\"kind\":\"rx\",\"hex\":\""+"AB".repeat(1000)+"\"}\n").getBytes(StandardCharsets.UTF_8);for(int i=0;i<4000;i++)out.write(block);out.write("{\"kind\":\"end\",\"transport_complete\":true}\n".getBytes(StandardCharsets.UTF_8));}
        assertTrue(RecordSummary.read(f).complete);
    }
}
