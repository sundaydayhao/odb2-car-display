package com.vehicle.capture;

import org.junit.Test;
import static org.junit.Assert.*;

public class YmObdHandshakeTest {
    private static final String PREFIX="SHENZHEN YUMING ELECTRONICS CO., LTD.\nDEVICE TYPE:B02-Z\nCRYPT:";
    @Test public void responseUsesUnsignedBytesAndThirtyTwoBitShifts() {
        assertEquals(0x0e7e7e7e,YmObdHandshake.response(0));
        assertEquals(0xdefefead,YmObdHandshake.response(0x12345678));
        assertEquals(0xd89b4fad,YmObdHandshake.response(0x6997eb7c));
        assertEquals(0x80ff7fff,YmObdHandshake.response(0xffffffff));
        assertEquals(0x0efe0000,YmObdHandshake.response(0x80000000));
    }
    @Test public void actualAdapterVersionProducesPerSessionResponse() {
        assertEquals("AT+SETCRYPTD89B4FAD",YmObdHandshake.command(PREFIX+"6997EB7C\n"));
        assertEquals("AT+SETCRYPTDEFEFEAD",YmObdHandshake.command(PREFIX+"12345678\n"));
    }
    @Test public void ordinaryElmWithoutChallengeRemainsCompatible() {
        assertNull(YmObdHandshake.command("?\n"));assertNull(YmObdHandshake.command("OK\n"));
    }
    @Test public void unknownManufacturerOrMalformedChallengeIsRefused() {
        for(String version:new String[]{"OTHER DEVICE\nCRYPT:12345678\n",PREFIX+"1234\n",PREFIX+"12345678\nCRYPT:87654321\n",PREFIX+"12345678ATZ\n"}) {
            try { YmObdHandshake.command(version);fail(version); } catch(IllegalArgumentException expected) {}
        }
    }
}
