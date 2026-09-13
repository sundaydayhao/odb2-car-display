package com.vehicle.capture;

import android.annotation.SuppressLint;
import android.app.*;
import android.bluetooth.*;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.os.*;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;

@SuppressLint("MissingPermission") // Activity checks runtime permissions; calls also catch revocation.
public final class CaptureService extends Service implements ElmSession.Sink {
    public static final String CONNECT="connect", STOP="stop";
    public final class LocalBinder extends Binder { CaptureService service() { return CaptureService.this; } }
    private final LocalBinder binder=new LocalBinder();
    private HandlerThread thread; private Handler work;
    private BluetoothGatt gatt; private BluetoothGattCharacteristic tx,rx;
    private ElmSession session; private CaptureFile file;
    private PowerManager.WakeLock wake;
    private long linkDeadline, rxUs;
    private final AtomicInteger pendingBytes=new AtomicInteger();
    private final AtomicBoolean overflowPosted=new AtomicBoolean();
    public volatile String status="未连接", result="";
    public volatile boolean ready, active, connecting, hasSession, monitorMode;
    public volatile long frames, notifications, captureStartedMs, captureFinishedMs;
    public volatile int requestedSeconds;
    public volatile String adapterName="";
    public long captureElapsedMs() {
        long start=captureStartedMs;
        return start==0?0:Math.max(0,(captureFinishedMs==0?SystemClock.elapsedRealtime():captureFinishedMs)-start);
    }
    private String adapter="";
    private JSONObject bluetoothDetails;
    private static UUID uuid(String shortId) { return UUID.fromString("0000"+shortId+"-0000-1000-8000-00805f9b34fb"); }
    private static final UUID CCCD=uuid("2902");
    private final Runnable tick=new Runnable() { public void run() {
        if(session!=null && !session.isFinished()) session.tick(SystemClock.elapsedRealtime());
        if(gatt!=null && !ready && SystemClock.elapsedRealtime()>linkDeadline) fail("连接或服务发现超时");
        work.postDelayed(this,100);
    }};
    @Override public void onCreate() {
        super.onCreate(); thread=new HandlerThread("vehicle-capture"); thread.start(); work=new Handler(thread.getLooper());
        work.post(tick);
        NotificationManager nm=getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel("capture","车辆采集",NotificationManager.IMPORTANCE_LOW));
        wake=getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,getPackageName()+":capture");
        wake.setReferenceCounted(false);
    }
    @Override public IBinder onBind(Intent intent) { return binder; }
    private void foreground() {
        Intent open=new Intent(this,MainActivity.class);
        PendingIntent pi=PendingIntent.getActivity(this,0,open,PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        PendingIntent stop=PendingIntent.getService(this,1,new Intent(this,CaptureService.class).setAction(STOP),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        Notification n=new Notification.Builder(this,"capture").setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("车况采集正在连接或记录").setContentText("点击返回；停止将封存本地记录")
            .setContentIntent(pi).setOngoing(true).addAction(new Notification.Action.Builder(null,"停止",stop).build()).build();
        if(Build.VERSION.SDK_INT>=29) startForeground(1,n,ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
        else startForeground(1,n);
    }
    @Override public int onStartCommand(Intent intent,int flags,int startId) {
        if(intent==null) return START_NOT_STICKY;
        if(STOP.equals(intent.getAction())) { stopCapture(); return START_NOT_STICKY; }
        try { foreground(); } catch(SecurityException e) { status="缺少蓝牙权限，请重新授权"; stopSelf(); return START_NOT_STICKY; }
        String address=intent.getStringExtra("address");
        if(address!=null) work.post(()->connect(address));
        return START_NOT_STICKY;
    }
    private void connect(String address) {
        if(active || connecting) { return; }
        closeGatt(); ready=false; result=""; bluetoothDetails=null; status="正在连接，请确认副屏已释放适配器";
        connecting=true; hasSession=false; frames=notifications=0; captureStartedMs=captureFinishedMs=0;
        linkDeadline=SystemClock.elapsedRealtime()+30000;
        try {
            BluetoothAdapter a=getSystemService(BluetoothManager.class).getAdapter();
            if(a==null || !a.isEnabled()) throw new IllegalStateException("请开启手机蓝牙");
            BluetoothDevice device=a.getRemoteDevice(address);
            adapter=device.getName()==null?"BLE adapter":device.getName();
            adapterName=adapter;
            gatt=device.connectGatt(this,false,callback,BluetoothDevice.TRANSPORT_LE);
            if(gatt==null) fail("无法创建蓝牙连接");
        } catch(SecurityException | IllegalArgumentException | IllegalStateException e) { fail(e.getMessage()); }
    }
    private final BluetoothGattCallback callback=new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt g,int code,int state) { work.post(()->{
            if(g!=gatt) return;
            if(code!=BluetoothGatt.GATT_SUCCESS || state==BluetoothProfile.STATE_DISCONNECTED) { fail("蓝牙连接已断开 ("+code+")"); return; }
            if(state==BluetoothProfile.STATE_CONNECTED) {
                try { if(!g.discoverServices()) fail("无法发现服务"); }
                catch(SecurityException e) { fail("蓝牙权限已撤销"); }
            }
        }); }
        @Override public void onServicesDiscovered(BluetoothGatt g,int code) { work.post(()->{
            if(g!=gatt) return;
            if(code!=BluetoothGatt.GATT_SUCCESS) { fail("服务发现失败"); return; }
            saveServiceInventory(g);
            String[][] profiles={
                {uuid("fff0").toString(),uuid("fff2").toString(),uuid("fff1").toString()},
                {uuid("ffe0").toString(),uuid("ffe1").toString(),uuid("ffe1").toString()},
                {"6e400001-b5a3-f393-e0a9-e50e24dcca9e","6e400002-b5a3-f393-e0a9-e50e24dcca9e","6e400003-b5a3-f393-e0a9-e50e24dcca9e"}};
            for(String[] p:profiles) {
                BluetoothGattService s=g.getService(UUID.fromString(p[0]));
                if(s==null) continue;
                BluetoothGattCharacteristic t=s.getCharacteristic(UUID.fromString(p[1])),r=s.getCharacteristic(UUID.fromString(p[2]));
                if(t!=null && r!=null && (t.getProperties()&(BluetoothGattCharacteristic.PROPERTY_WRITE|BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE))!=0 &&
                    (r.getProperties()&(BluetoothGattCharacteristic.PROPERTY_NOTIFY|BluetoothGattCharacteristic.PROPERTY_INDICATE))!=0) { tx=t;rx=r;break; }
            }
            if(tx==null) { fail("首版不支持此适配器的串口服务"); return; }
            try {
                BluetoothGattDescriptor descriptor=rx.getDescriptor(CCCD);
                if(descriptor==null || !g.setCharacteristicNotification(rx,true)) { fail("无法订阅车辆数据"); return; }
                byte[] value=(rx.getProperties()&BluetoothGattCharacteristic.PROPERTY_NOTIFY)!=0?BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE:BluetoothGattDescriptor.ENABLE_INDICATION_VALUE;
                boolean ok;
                if(Build.VERSION.SDK_INT>=33) ok=g.writeDescriptor(descriptor,value)==BluetoothStatusCodes.SUCCESS;
                else { descriptor.setValue(value); ok=g.writeDescriptor(descriptor); }
                if(!ok) fail("通知订阅未提交");
            } catch(SecurityException e) { fail("蓝牙权限已撤销"); }
        }); }
        @Override public void onDescriptorWrite(BluetoothGatt g,BluetoothGattDescriptor d,int code) { work.post(()->{
            if(g!=gatt) return;
            if(code!=BluetoothGatt.GATT_SUCCESS) { fail("订阅失败，请检查适配器配对要求 ("+code+")"); return; }
            connecting=false; ready=true; status="已连接，开始采集时验证适配器";
        }); }
        @Override public void onCharacteristicWrite(BluetoothGatt g,BluetoothGattCharacteristic c,int code) { work.post(()->{
            if(g==gatt && session!=null) session.writeComplete(code==BluetoothGatt.GATT_SUCCESS,SystemClock.elapsedRealtime());
        }); }
        @Override public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c,byte[] value) { receive(g,value); }
        @Override public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c) {
            if(Build.VERSION.SDK_INT<33) receive(g,c.getValue());
        }
    };
    /** Preserve the actual adapter layout, including unsupported profiles, for board bring-up. */
    private void saveServiceInventory(BluetoothGatt g) {
        try {
            JSONArray services=new JSONArray();
            for(BluetoothGattService service:g.getServices()) {
                JSONArray characteristics=new JSONArray();
                for(BluetoothGattCharacteristic characteristic:service.getCharacteristics()) {
                    JSONArray descriptors=new JSONArray();
                    for(BluetoothGattDescriptor descriptor:characteristic.getDescriptors()) descriptors.put(descriptor.getUuid().toString());
                    characteristics.put(new JSONObject().put("uuid",characteristic.getUuid().toString())
                        .put("properties",characteristic.getProperties()).put("descriptors",descriptors));
                }
                services.put(new JSONObject().put("uuid",service.getUuid().toString()).put("characteristics",characteristics));
            }
            JSONObject inventory=new JSONObject().put("schema","vehicle-ble-services/1").put("utc_ms",System.currentTimeMillis())
                .put("adapter",adapter).put("address",g.getDevice().getAddress()).put("services",services);
            bluetoothDetails=inventory;
            try(FileOutputStream out=openFileOutput("last-bluetooth-services.json",MODE_PRIVATE)) {
                out.write((inventory.toString(2)+"\n").getBytes(StandardCharsets.UTF_8));
            }
        } catch(Exception e) { android.util.Log.w("VehicleCapture","Unable to save Bluetooth service inventory",e); }
    }
    private void receive(BluetoothGatt g,byte[] value) {
        if(value==null || value.length==0) return;
        long delivered=SystemClock.elapsedRealtimeNanos()/1000;
        if(pendingBytes.addAndGet(value.length)>256*1024) {
            pendingBytes.addAndGet(-value.length);
            if(overflowPosted.compareAndSet(false,true)) work.post(()->{
                if(g==gatt) fail("手机接收队列溢出，记录可能丢包");
                overflowPosted.set(false);
            });
            return;
        }
        byte[] copy=value.clone();
        work.post(()->{
            pendingBytes.addAndGet(-copy.length);
            if(g!=gatt || session==null || session.isFinished()) return;
            rxUs=delivered;
            try { file.raw(copy,delivered); ++notifications; session.bytes(copy,SystemClock.elapsedRealtime()); }
            catch(IOException e) { fail("记录写入失败: "+e.getMessage()); }
        });
    }
    public void startCapture(boolean monitor,int protocol,String filter,int seconds,boolean physical) { work.post(()->{
        if(!ready || active) return;
        try {
            session=new ElmSession(this,monitor,protocol,filter,seconds,physical);
            file=new CaptureFile(records(),monitor,protocol,filter,seconds,adapter,bluetoothDetails,physical);
            frames=notifications=0; captureStartedMs=captureFinishedMs=0; requestedSeconds=seconds; monitorMode=monitor; hasSession=true;
            active=true; wake.acquire((seconds+120)*1000L);
            status="正在初始化适配器"; session.start(SystemClock.elapsedRealtime());
        } catch(IOException | IllegalArgumentException e) { session=null; status=e.getMessage(); }
    }); }
    public void stopCapture() { work.post(()->{
        if(session!=null && !session.isFinished()) { status="正在停止并封存"; session.stop("user",SystemClock.elapsedRealtime()); }
        else { closeGatt(); status="已断开"; stopForeground(STOP_FOREGROUND_REMOVE); stopSelf(); }
    }); }
    public File records() { return new File(getFilesDir(),"captures"); }
    @Override public boolean write(byte[] value) {
        if(gatt==null || tx==null) return false;
        int type=(tx.getProperties()&BluetoothGattCharacteristic.PROPERTY_WRITE)!=0?BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT:BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE;
        try {
            if(Build.VERSION.SDK_INT>=33) return gatt.writeCharacteristic(tx,value,type)==BluetoothStatusCodes.SUCCESS;
            tx.setWriteType(type); tx.setValue(value); return gatt.writeCharacteristic(tx);
        } catch(SecurityException e) { return false; }
    }
    @Override public void event(String kind,String value) {
        try {
            boolean received=kind.equals("line") || kind.equals("response") || kind.equals("unparsed") || kind.equals("partial_line");
            if(file!=null) file.event(kind,value,received && rxUs!=0?rxUs:SystemClock.elapsedRealtimeNanos()/1000);
            if(kind.equals("capture_started")) { captureStartedMs=SystemClock.elapsedRealtime(); status=value.equals("can_monitor")?"正在监听 CAN，等待可见报文":"正在记录诊断请求与响应"; }
        } catch(IOException e) { if(session!=null) session.fail("记录空间不足或写入失败"); }
    }
    @Override public void frame(CanFrame frame) {
        try { if(file!=null) { file.frame(frame,rxUs); frames=file.frames; } }
        catch(IOException e) { fail("记录写入失败"); }
    }
    @Override public void done(String reason,boolean complete) {
        captureFinishedMs=SystemClock.elapsedRealtime();
        active=false;
        try { if(file!=null) { File saved=file.finish(reason,complete); result=saved.getName(); } }
        catch(IOException e) { result="记录片段已保留，请检查文件列表"; complete=false; }
        file=null;
        String displayReason="duration".equals(reason)?"时长已到":"user".equals(reason)?"手动停止":reason;
        status=(complete?"采集已完成: ":"采集已中断: ")+displayReason;
        if(wake.isHeld()) wake.release();
        /* Always release ownership. A new session requires a fresh connection. */
        closeGatt(); stopForeground(STOP_FOREGROUND_REMOVE); stopSelf();
    }
    private void fail(String reason) {
        if(session!=null && !session.isFinished()) session.fail(reason);
        else { closeGatt(); status=reason; stopForeground(STOP_FOREGROUND_REMOVE); stopSelf(); }
    }
    private void closeGatt() {
        ready=false; connecting=false; tx=rx=null;
        if(gatt!=null) { BluetoothGatt old=gatt; gatt=null; try { old.disconnect(); } catch(SecurityException ignored) {} old.close(); }
    }
    @Override public void onDestroy() {
        work.removeCallbacks(tick);
        work.post(()->{ if(session!=null && !session.isFinished()) session.fail("服务已停止"); closeGatt(); if(wake.isHeld()) wake.release(); thread.quitSafely(); });
        super.onDestroy();
    }
}
