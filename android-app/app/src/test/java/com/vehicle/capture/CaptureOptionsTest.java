package com.vehicle.capture;

import org.junit.Test;
import static org.junit.Assert.*;

public class CaptureOptionsTest {
    @Test public void acceptsNormalized11bitAnd29bitFilters() {
        assertEquals("7E8",CaptureOptions.filter(" 7e8 ",6));
        assertEquals("1FFFFFFF",CaptureOptions.filter("1fffffff",7));
        assertEquals("",CaptureOptions.filter(" ",9));
    }
    @Test public void rejectsWrongAddressWidthAndOutOfRange() {
        for(String s:new String[]{"7E80","800","0x7E8","ZZZ","18DAF110"})assertThrows(IllegalArgumentException.class,()->CaptureOptions.filter(s,6));
        for(String s:new String[]{"7E8","20000000","FFFFFFFF"})assertThrows(IllegalArgumentException.class,()->CaptureOptions.filter(s,9));
        assertThrows(IllegalArgumentException.class,()->CaptureOptions.filter("",10));
    }
    @Test public void corruptedPreferenceIndicesRecoverToDefaults() {
        assertEquals(1,CaptureOptions.index(20,4,1));assertEquals(0,CaptureOptions.index(-1,3,0));
        assertEquals(2,CaptureOptions.index(2,3,0));
    }
}
