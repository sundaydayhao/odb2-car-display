package com.vehicle.capture;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.*;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.location.LocationManager;
import android.net.Uri;
import android.os.*;
import android.provider.Settings;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

@SuppressLint("MissingPermission")
public final class MainActivity extends Activity {
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService filesWorker=Executors.newSingleThreadExecutor();
    private SharedPreferences prefs;
    private CaptureService service;
    private boolean bound, resumed, scanning, dark, readingFiles, pendingScan;
    private int page, category, recordFilter, mode, duration, protocol;
    private int bg, surface, ink, muted, accent, tint, line, warning;
    private String filter, listedResult="";
    private boolean physical;
    private LinearLayout root, content, deviceList;
    private TextView connectionTitle, connectionDetail, stateBadge, metricCount, metricLabel, elapsed, captureNote, scanHint;
    private Button primary, secondary, scanButton;
    private ProgressBar captureProgress;
    private BluetoothLeScanner scanner;
    private AlertDialog deviceDialog;
    private final HashSet<String> seen=new HashSet<>();
    private final ArrayList<RecordSummary> records=new ArrayList<>();
    private File exportFile;
    private final Runnable scanTimeout=this::stopScan;
    private final Runnable pulse=new Runnable() { public void run() { update(); if(resumed) main.postDelayed(this,500); } };
    private final ServiceConnection connection=new ServiceConnection() {
        public void onServiceConnected(ComponentName name,IBinder binder) { service=((CaptureService.LocalBinder)binder).service(); update(); refreshFiles(); }
        public void onServiceDisconnected(ComponentName name) { service=null; update(); }
    };
    @Override public void onCreate(Bundle saved) {
        prefs=getSharedPreferences("interface",MODE_PRIVATE);
        int appearance=CaptureOptions.index(prefs.getInt("theme",0),3,0);
        dark=appearance==2 || appearance==0 && (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)==Configuration.UI_MODE_NIGHT_YES;
        setTheme(dark?R.style.AppTheme_Dark:R.style.AppTheme);
        super.onCreate(saved);
        bg=color(dark?"#101923":"#F3F6F8"); surface=color(dark?"#1A2633":"#FFFFFF");
        ink=color(dark?"#EDF4FA":"#172D40"); muted=color(dark?"#A7B6C5":"#617588");
        accent=color(dark?"#61D5C3":"#006D62"); tint=color(dark?"#203F41":"#E4F3F0");
        line=color(dark?"#314251":"#E3EAF0"); warning=color(dark?"#F3BD75":"#935713");
        getWindow().setStatusBarColor(bg); getWindow().setNavigationBarColor(surface);
        getWindow().getDecorView().setSystemUiVisibility(dark?0:View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR|View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        mode=CaptureOptions.index(prefs.getInt("mode",0),2,0); duration=CaptureOptions.index(prefs.getInt("duration",1),4,1);
        protocol=CaptureOptions.index(prefs.getInt("protocol",0),4,0); filter=prefs.getString("filter",""); physical=prefs.getBoolean("physical",false);
        if(saved!=null) { page=CaptureOptions.index(saved.getInt("page"),3,0); category=saved.getInt("category"); recordFilter=saved.getInt("recordFilter");
            String name=saved.getString("export"); if(name!=null && name.equals(new File(name).getName())) exportFile=new File(directory(),name); }
        render(); bound=bindService(new Intent(this,CaptureService.class),connection,BIND_AUTO_CREATE); refreshFiles();
    }
    @Override protected void onSaveInstanceState(Bundle saved) { saved.putInt("page",page); saved.putInt("category",category); saved.putInt("recordFilter",recordFilter); if(exportFile!=null) saved.putString("export",exportFile.getName()); super.onSaveInstanceState(saved); }
    @Override protected void onResume() { super.onResume(); resumed=true; main.removeCallbacks(pulse); main.post(pulse); if(page==2) render(); }
    @Override protected void onPause() { resumed=false; main.removeCallbacks(pulse); stopScan(); super.onPause(); }
    @Override protected void onDestroy() { main.removeCallbacksAndMessages(null); if(deviceDialog!=null) deviceDialog.dismiss(); if(bound) unbindService(connection); filesWorker.shutdown(); super.onDestroy(); }
    private void saveOptions() { prefs.edit().putInt("mode",mode).putInt("duration",duration).putInt("protocol",protocol).putString("filter",filter).putBoolean("physical",physical).apply(); }
    private int color(String value) { return Color.parseColor(value); }
    private int dp(int value) { return Math.round(value*getResources().getDisplayMetrics().density); }
    private LinearLayout column() { LinearLayout v=new LinearLayout(this); v.setOrientation(LinearLayout.VERTICAL); return v; }
    private LinearLayout row() { LinearLayout v=new LinearLayout(this); v.setGravity(Gravity.CENTER_VERTICAL); return v; }
    private GradientDrawable shape(int fill,int radius,int stroke) { GradientDrawable d=new GradientDrawable(); d.setColor(fill); d.setCornerRadius(dp(radius)); if(stroke!=0)d.setStroke(dp(1),stroke); return d; }
    private void clickable(View v,int fill,int radius,Runnable action) { v.setBackground(new RippleDrawable(ColorStateList.valueOf(dark?0x2261D5C3:0x22006D62),shape(fill,radius,0),null)); v.setOnClickListener(w->action.run()); }
    private TextView text(String value,int size,int color,boolean bold) { TextView v=new TextView(this); v.setText(value); v.setTextSize(size); v.setTextColor(color); v.setFontFeatureSettings("tnum"); if(bold)v.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL)); v.setLineSpacing(dp(2),1f); return v; }
    private TextView label(LinearLayout parent,String value,int size,int color,boolean bold) { TextView v=text(value,size,color,bold); parent.addView(v); return v; }
    private void gap(LinearLayout parent,int height) { Space s=new Space(this); parent.addView(s,new LinearLayout.LayoutParams(1,dp(height))); }
    private void divider(LinearLayout parent) { View d=new View(this); d.setBackgroundColor(line); parent.addView(d,new LinearLayout.LayoutParams(-1,dp(1))); }
    private AppIcon icon(LinearLayout parent,String name,int color,int size) { AppIcon v=new AppIcon(this,name,color); parent.addView(v,new LinearLayout.LayoutParams(dp(size),dp(size))); return v; }
    private LinearLayout card(LinearLayout parent) { LinearLayout v=column(); v.setPadding(dp(20),dp(20),dp(20),dp(20)); v.setBackground(shape(surface,20,0)); LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2); lp.bottomMargin=dp(14); parent.addView(v,lp); return v; }
    private Button button(LinearLayout parent,String title,boolean filled,Runnable action) {
        Button b=new Button(this); b.setText(title); b.setTextSize(15); b.setAllCaps(false); b.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
        b.setTextColor(filled?(dark?color("#102D2B"):Color.WHITE):accent); b.setMinHeight(dp(52)); b.setMinimumHeight(dp(52)); b.setPadding(dp(14),dp(10),dp(14),dp(10));
        clickable(b,filled?accent:tint,14,action); LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2); p.topMargin=dp(10); parent.addView(b,p); return b;
    }
    private void enabled(Button button,boolean enabled) { if(button!=null) { button.setEnabled(enabled); button.setAlpha(enabled?1:.45f); } }
    private void detailRow(LinearLayout parent,String icon,String title,String subtitle,Runnable action) {
        LinearLayout r=row(); r.setPadding(dp(2),dp(15),dp(2),dp(15)); r.setMinimumHeight(dp(64));
        if(icon!=null) { AppIcon v=icon(r,icon,accent,22); ((LinearLayout.LayoutParams)v.getLayoutParams()).rightMargin=dp(14); }
        LinearLayout words=column(); label(words,title,16,ink,true); if(subtitle!=null && !subtitle.isEmpty()){gap(words,4);label(words,subtitle,13,muted,false);}
        r.addView(words,new LinearLayout.LayoutParams(0,-2,1)); if(action!=null) {icon(r,"arrow",muted,17);clickable(r,surface,10,action);r.setFocusable(true);}
        parent.addView(r,new LinearLayout.LayoutParams(-1,-2));
    }
    private boolean busy() { return service!=null && service.active; }
    private boolean connecting() { return service!=null && service.connecting; }
    private void openPage(int value) { page=value; render(); if(value==1)refreshFiles(); }
    private void render() {
        connectionTitle=connectionDetail=stateBadge=metricCount=metricLabel=elapsed=captureNote=null; primary=secondary=null; captureProgress=null;
        root=column(); root.setBackgroundColor(bg); setContentView(root);
        LinearLayout header=row(); header.setPadding(dp(24),dp(12),dp(18),dp(12)); LinearLayout heading=column();
        label(heading,"VEHICLE CAPTURE",10,accent,true).setLetterSpacing(.16f); gap(heading,5); label(heading,new String[]{"车况采集","采集记录","设置"}[page],27,ink,true); header.addView(heading,new LinearLayout.LayoutParams(0,-2,1));
        if(page!=2) { LinearLayout gear=row(); gear.setGravity(Gravity.CENTER); icon(gear,"settings",ink,23); gear.setContentDescription("打开设置"); gear.setFocusable(true); clickable(gear,surface,16,()->openPage(2)); header.addView(gear,new LinearLayout.LayoutParams(dp(48),dp(48))); } root.addView(header);
        ScrollView scroll=new ScrollView(this); scroll.setFillViewport(true); scroll.setClipToPadding(false); scroll.setVerticalScrollBarEnabled(false);
        FrameLayout holder=new FrameLayout(this); content=column(); content.setPadding(dp(20),dp(8),dp(20),dp(20));
        boolean wide=getResources().getConfiguration().screenWidthDp>=720; int max=page==2 && wide?1000:760;
        FrameLayout.LayoutParams lp=new FrameLayout.LayoutParams(Math.min(getResources().getDisplayMetrics().widthPixels,dp(max)),-2,Gravity.TOP|Gravity.CENTER_HORIZONTAL);
        holder.addView(content,lp); scroll.addView(holder); root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        if(page==0) renderCapture(); else if(page==1) renderRecords(); else renderSettings(wide);
        LinearLayout nav=row(); nav.setPadding(dp(12),dp(7),dp(12),dp(7)); nav.setBackgroundColor(surface);
        String[] titles={"采集","记录","设置"},icons={"capture","records","settings"};
        for(int i=0;i<3;i++) { final int target=i; LinearLayout item=column(); item.setGravity(Gravity.CENTER); item.setPadding(dp(10),dp(8),dp(10),dp(8));
            icon(item,icons[i],i==page?accent:muted,23); gap(item,4); label(item,titles[i],12,i==page?accent:muted,i==page).setGravity(Gravity.CENTER);
            item.setContentDescription(titles[i]+(i==page?"，当前页面":"")); item.setFocusable(true); item.setSelected(i==page); clickable(item,i==page?tint:surface,16,()->openPage(target));
            LinearLayout.LayoutParams itemLp=new LinearLayout.LayoutParams(0,-2,1); itemLp.setMargins(dp(4),0,dp(4),0); nav.addView(item,itemLp);
        } divider(root); root.addView(nav); update();
    }
    private void renderCapture() {
        LinearLayout device=card(content),top=row(); icon(top,"bluetooth",accent,23);
        TextView caption=text("车辆适配器",14,muted,true); LinearLayout.LayoutParams cp=new LinearLayout.LayoutParams(0,-2,1); cp.leftMargin=dp(10); top.addView(caption,cp);
        stateBadge=text("未连接",12,muted,true); stateBadge.setPadding(dp(10),dp(5),dp(10),dp(5)); stateBadge.setBackground(shape(tint,12,0)); top.addView(stateBadge); device.addView(top);
        gap(device,20); connectionTitle=label(device,"连接你的车辆",23,ink,true); gap(device,8); connectionDetail=label(device,"连接 BLE 适配器，开始一次本地记录。",14,muted,false);
        gap(device,12); primary=button(device,"选择适配器",true,()->{if(service!=null && service.ready && !busy()) startCapture(); else if(!connecting() && !busy()) showDevices();});
        secondary=button(device,"连接上次设备",false,this::connectLast);
        LinearLayout sessionCard=card(content),title=row(); icon(title,"capture",accent,22); TextView sessionHeading=text("本次采集",16,ink,true); LinearLayout.LayoutParams sp=new LinearLayout.LayoutParams(-2,-2);sp.leftMargin=dp(10);title.addView(sessionHeading,sp);sessionCard.addView(title);
        gap(sessionCard,18); LinearLayout metrics=row(),count=column(),time=column(); metricCount=label(count,"0",32,ink,true); metricLabel=label(count,"接收通知",12,muted,false); elapsed=label(time,"00:00",32,ink,true);label(time,"已采集时间",12,muted,false);metrics.addView(count,new LinearLayout.LayoutParams(0,-2,1));metrics.addView(time,new LinearLayout.LayoutParams(0,-2,1));sessionCard.addView(metrics);
        gap(sessionCard,18); captureProgress=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);captureProgress.setMax(1000);captureProgress.setProgressTintList(ColorStateList.valueOf(accent));captureProgress.setProgressBackgroundTintList(ColorStateList.valueOf(line));sessionCard.addView(captureProgress,new LinearLayout.LayoutParams(-1,dp(5)));
        gap(sessionCard,12);captureNote=label(sessionCard,"记录自动保存在本机，可在记录页导出。",13,muted,false);
        LinearLayout plan=card(content);label(plan,"采集方案",12,muted,true);
        detailRow(plan,"records","采集方式",CaptureOptions.MODES[mode],this::chooseMode);divider(plan);
        detailRow(plan,"clock","记录时长",CaptureOptions.DURATIONS[duration]+" · 到时自动保存并断开",this::chooseDuration);
        gap(content,2);label(content,"请在驻车时操作。手机采集结束后，副屏可直接连接适配器。",12,muted,false);
    }
    private void putText(TextView view,String value) { if(!value.contentEquals(view.getText()))view.setText(value); }
    private void update() {
        if(isDestroyed()) return;
        boolean active=busy(),ready=service!=null && service.ready,linking=connecting();
        if(prefs.getBoolean("screen_on",false) && active)getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if(service!=null && !service.result.equals(listedResult)) { listedResult=service.result; refreshFiles(); }
        if(connectionTitle==null) return;
        String name=service!=null && !service.adapterName.isEmpty()?service.adapterName:prefs.getString("device_name","车辆适配器");
        putText(stateBadge,active?"采集中":ready?"已连接":linking?"连接中":"未连接");stateBadge.setTextColor(active||ready?accent:muted);
        putText(connectionTitle,active?"正在记录车辆数据":ready?name:linking?"正在连接适配器":"连接你的车辆");
        putText(connectionDetail,service==null || "未连接".equals(service.status)?"选择附近的 BLE 适配器，开始本地记录。":service.status);
        putText(primary,active?"采集进行中":ready?"开始采集 · "+CaptureOptions.DURATIONS[duration]:linking?"正在连接…":"选择适配器");enabled(primary,!active && !linking);
        putText(secondary,active?"停止并保存":linking?"取消连接":ready?"断开适配器":"连接上次设备");
        secondary.setVisibility(active||ready||linking||!prefs.getString("device_address","").isEmpty()?View.VISIBLE:View.GONE);
        if(!active && !ready && !linking) secondary.setOnClickListener(v->connectLast());else secondary.setOnClickListener(v->{if(service!=null)service.stopCapture();});
        boolean monitor=service!=null && service.hasSession?service.monitorMode:mode==1;
        putText(metricLabel,monitor?"已解析 CAN 帧":"接收通知（含初始化）"); putText(metricCount,String.valueOf(service==null?0:monitor?service.frames:service.notifications));
        long millis=service==null?0:service.captureElapsedMs();long seconds=millis/1000;putText(elapsed,String.format(Locale.ROOT,"%02d:%02d",seconds/60,seconds%60));
        int limit=service!=null && service.hasSession?service.requestedSeconds:CaptureOptions.SECONDS[duration];
        captureProgress.setProgress((int)Math.min(1000,millis*1000/Math.max(1,limit*1000L)));
        putText(captureNote,active?(service.captureStartedMs==0?"正在验证适配器，验证完成后开始计时。":"剩余约 "+Math.max(0,limit-seconds)+" 秒 · 保持蓝牙连接"):
            service!=null && !service.result.isEmpty()?"本次记录已保存，可前往「记录」查看和导出。":"到时自动保存。结束后会释放适配器连接。");
    }
    private void renderRecords() {
        long total=0;for(RecordSummary r:records)total+=r.file.length();
        LinearLayout summary=card(content); label(summary,records.size()+" 份本地记录",22,ink,true);gap(summary,6);label(summary,formatBytes(total)+" 已使用 · 全部保存在此手机",13,muted,false);
        LinearLayout chips=row();String[] labels={"全部","已完成","需检查"};
        for(int i=0;i<3;i++){final int selected=i;TextView chip=text(labels[i],13,recordFilter==i?accent:muted,true);chip.setGravity(Gravity.CENTER);chip.setPadding(dp(8),dp(14),dp(8),dp(14));clickable(chip,recordFilter==i?tint:surface,12,()->{recordFilter=selected;render();});LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,-2,1);p.rightMargin=dp(4);chips.addView(chip,p);}gap(summary,16);summary.addView(chips);
        int shown=0;
        for(RecordSummary r:records) {
            if(recordFilter==1 && !r.complete || recordFilter==2 && r.complete)continue;shown++;
            LinearLayout card=card(content),top=row();icon(top,"records",accent,24);TextView title=text(r.title(),17,ink,true);LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,-2,1);p.leftMargin=dp(12);top.addView(title,p);
            TextView badge=text(r.complete?"已完成":"需检查",12,r.complete?accent:warning,true);top.addView(badge);card.addView(top);gap(card,13);
            label(card,date(r.date)+" · "+formatBytes(r.file.length()),13,muted,false);gap(card,5);label(card,r.adapter,13,muted,false);button(card,"查看记录",false,()->showRecord(r));
        }
        if(shown==0){LinearLayout empty=card(content);icon(empty,"folder",muted,36);gap(empty,18);label(empty,records.isEmpty()?"还没有采集记录":"没有这类记录",20,ink,true);gap(empty,8);label(empty,records.isEmpty()?"连接适配器并完成一次采集，原始记录会出现在这里。":"可切换到「全部」查看其他记录。",14,muted,false);if(records.isEmpty())button(empty,"开始首次采集",true,()->openPage(0));}
        label(content,"“已完成”表示采集会话正常结束，不代表获取了全车数据或没有丢帧。卸载前请导出需要保留的记录。",12,muted,false);button(content,"刷新记录",false,this::refreshFiles);
    }
    private void renderSettings(boolean wide) {
        String[] groups={"连接与权限","采集偏好","外观与显示","数据与关于"};String[] icons={"bluetooth","capture","sun","shield"};
        if(wide) {
            LinearLayout split=new LinearLayout(this);split.setGravity(Gravity.TOP);LinearLayout menu=column(),detail=column();menu.setPadding(dp(14),dp(10),dp(14),dp(10));menu.setBackground(shape(surface,20,0));
            for(int i=0;i<groups.length;i++){final int selected=i;TextView choice=text(groups[i],15,category==i?accent:ink,category==i);choice.setPadding(dp(16),dp(18),dp(16),dp(18));clickable(choice,category==i?tint:surface,12,()->{category=selected;render();});menu.addView(choice);}
            LinearLayout.LayoutParams left=new LinearLayout.LayoutParams(dp(210),-2);left.rightMargin=dp(18);split.addView(menu,left);split.addView(detail,new LinearLayout.LayoutParams(0,-2,1));content.addView(split);settingsGroup(detail,CaptureOptions.index(category,4,0),groups,icons);
        } else for(int i=0;i<groups.length;i++)settingsGroup(content,i,groups,icons);
    }
    private void settingsGroup(LinearLayout parent,int group,String[] groups,String[] icons) {
        LinearLayout c=card(parent),title=row();icon(title,icons[group],accent,20);TextView heading=text(groups[group],13,muted,true);LinearLayout.LayoutParams hp=new LinearLayout.LayoutParams(-2,-2);hp.leftMargin=dp(10);title.addView(heading,hp);c.addView(title);gap(c,8);
        if(group==0) {
            detailRow(c,null,"蓝牙适配器",prefs.getString("device_name","尚未选择")+" · 点击搜索或更换",this::showDevices);divider(c);
            detailRow(c,null,"附近设备权限",bluetoothPermission()?"已允许":"未允许 · 点击管理",()->{if(bluetoothPermission())appSettings();else requestBluetooth(false);});divider(c);
            detailRow(c,null,"采集通知",getSystemService(NotificationManager.class).areNotificationsEnabled()?"已开启 · 可从通知栏停止采集":"未开启 · 点击管理",this::notificationSettings);
            if(!prefs.getString("device_address","").isEmpty()){divider(c);detailRow(c,null,"忘记上次设备","仅清除 App 中保存的设备信息",()->{if(busy()||connecting()||service!=null&&service.ready){toast("请先断开适配器");return;}prefs.edit().remove("device_name").remove("device_address").apply();render();});}
        } else if(group==1) {
            detailRow(c,null,"默认采集方式",CaptureOptions.MODES[mode],this::chooseMode);divider(c);detailRow(c,null,"默认记录时长",CaptureOptions.DURATIONS[duration],this::chooseDuration);divider(c);
            detailRow(c,null,"CAN 监听协议",CaptureOptions.PROTOCOLS[protocol]+" · 仅用于监听",()->editChoice("CAN 监听协议",CaptureOptions.PROTOCOLS,protocol,i->{protocol=i;filter="";saveOptions();render();}));divider(c);
            detailRow(c,null,"CAN ID 过滤",filter.isEmpty()?"接收全部可见 ID · 仅用于监听":filter+" · 仅用于监听",this::editFilter);divider(c);
            Switch compatibility=new Switch(this);compatibility.setText(R.string.physical_diagnostic);compatibility.setTextSize(15);compatibility.setTextColor(ink);compatibility.setPadding(0,dp(14),0,dp(14));compatibility.setMinHeight(dp(52));compatibility.setChecked(physical);compatibility.setEnabled(!busy());compatibility.setOnCheckedChangeListener((b,value)->{physical=value;saveOptions();});c.addView(compatibility);label(c,"用于部分 11 位 CAN 适配器的诊断兼容；仅发送标准参数读取请求。",12,muted,false);
        } else if(group==2) {
            detailRow(c,null,"主题",CaptureOptions.THEMES[CaptureOptions.index(prefs.getInt("theme",0),3,0)],()->new AlertDialog.Builder(this).setTitle("外观主题").setSingleChoiceItems(CaptureOptions.THEMES,prefs.getInt("theme",0),(dialog,i)->{prefs.edit().putInt("theme",i).apply();dialog.dismiss();recreate();}).setNegativeButton("取消",null).show());divider(c);
            Switch awake=new Switch(this);awake.setText("采集时保持屏幕常亮");awake.setTextSize(15);awake.setTextColor(ink);awake.setPadding(0,dp(16),0,dp(16));awake.setMinHeight(dp(52));awake.setChecked(prefs.getBoolean("screen_on",false));awake.setOnCheckedChangeListener((b,value)->{prefs.edit().putBoolean("screen_on",value).apply();update();});c.addView(awake);label(c,"默认关闭；锁屏后仍由前台服务执行限时采集。",12,muted,false);
        } else {
            long total=0;for(RecordSummary r:records)total+=r.file.length();detailRow(c,null,"本地存储",records.size()+" 份记录 · "+formatBytes(total),()->openPage(1));divider(c);
            detailRow(c,null,"数据与隐私","本机保存 · 无账号 · 无云端上传",()->info("数据与隐私","采集文件保存在 App 私有目录，不会自动上传，也不参与系统备份。\n\n只有你主动导出时，文件才写入选择的位置。原始报文可能包含车辆信息，请自行决定分享范围。\n\n卸载 App 会删除尚未导出的本地记录。"));divider(c);
            detailRow(c,null,"适配器与采集说明","BLE 支持范围及记录说明",()->info("使用说明","支持 FFF0、FFE0、Nordic UART 三类 BLE 串口服务，暂不支持仅有经典蓝牙 SPP 的适配器。\n\n标准参数记录用于车速、转速、水温等请求与响应。CAN 监听仅记录诊断口可见报文，适配器和车辆网关会影响结果。\n\n时间戳来自手机接收时刻。B02-Z 已验证标准参数读取，原始 CAN 监听尚未验证通过。\n\n连接前请释放副屏或其他 App 的连接；采集结束会自动断开。"));divider(c);
            detailRow(c,null,"车况采集","版本 "+BuildConfig.VERSION_NAME+" · "+(BuildConfig.DEBUG?"开发验证版":"发布构建"),null);
        }
    }
    private interface Choice { void select(int value); }
    private void editChoice(String title,String[] values,int selected,Choice action) { if(busy()){toast("请在采集结束后修改方案");return;}new AlertDialog.Builder(this).setTitle(title).setSingleChoiceItems(values,selected,(dialog,i)->{if(!busy())action.select(i);dialog.dismiss();}).setNegativeButton("取消",null).show(); }
    private void chooseMode() { editChoice("采集方式",new String[]{"标准参数记录 · 自动识别协议","CAN 报文监听 · 需选择车辆协议"},mode,i->{mode=i;saveOptions();render();}); }
    private void chooseDuration() { editChoice("记录时长",CaptureOptions.DURATIONS,duration,i->{duration=i;saveOptions();render();}); }
    private void editFilter() {
        if(busy()){toast("请在采集结束后修改方案");return;}
        LinearLayout body=column();body.setPadding(dp(24),dp(8),dp(24),dp(8));label(body,"留空表示接收所有可见 ID。",14,muted,false);
        EditText input=new EditText(this);input.setSingleLine(true);input.setText(filter);input.setHint(protocol==0||protocol==2?"例如 7E8":"例如 18DAF110");input.setInputType(android.text.InputType.TYPE_CLASS_TEXT|android.text.InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS);input.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(8)});body.addView(input);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("CAN ID 过滤").setView(body).setNegativeButton("取消",null).setPositiveButton("保存",null).create();
        dialog.setOnShowListener(d->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{try{if(busy())return;filter=CaptureOptions.filter(input.getText().toString(),protocol+6);saveOptions();dialog.dismiss();render();}catch(IllegalArgumentException e){input.setError(e.getMessage());}}));dialog.show();
    }
    private boolean bluetoothPermission() {
        if(Build.VERSION.SDK_INT>=31)return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)==PackageManager.PERMISSION_GRANTED && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED;
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)==PackageManager.PERMISSION_GRANTED;
    }
    private void showDevices() {
        if(busy()||connecting()){toast("请先停止采集或取消连接");return;}if(service!=null && service.ready){toast("请先在采集页断开当前适配器");return;}
        if(deviceDialog!=null && deviceDialog.isShowing())return;
        LinearLayout body=column();body.setPadding(dp(20),dp(8),dp(20),dp(8));label(body,"先断开副屏或其他诊断 App，再选择你的 BLE 适配器。",14,muted,false);
        scanButton=button(body,"扫描附近设备",true,()->{if(scanning)stopScan();else requestBluetooth(true);});gap(body,12);scanHint=label(body,"点击扫描，发现设备后选择连接。",13,muted,false);
        ScrollView scroll=new ScrollView(this);deviceList=column();scroll.addView(deviceList);body.addView(scroll,new LinearLayout.LayoutParams(-1,dp(Math.min(330,getResources().getConfiguration().screenHeightDp/3))));
        String last=prefs.getString("device_address","");if(!last.isEmpty())detailRow(deviceList,"bluetooth",prefs.getString("device_name","上次设备"),last+" · 上次选择",()->selectDevice(last,prefs.getString("device_name","车辆适配器")));
        deviceDialog=new AlertDialog.Builder(this).setTitle("连接适配器").setView(body).setNegativeButton("关闭",null).create();
        deviceDialog.setOnDismissListener(d->{stopScan();deviceDialog=null;deviceList=null;scanHint=null;scanButton=null;});deviceDialog.show();
    }
    private void requestBluetooth(boolean scanAfter) {
        pendingScan=scanAfter;if(bluetoothPermission()){if(scanAfter)beginScan();return;}
        String permission=Build.VERSION.SDK_INT>=31?Manifest.permission.BLUETOOTH_SCAN:Manifest.permission.ACCESS_FINE_LOCATION;
        if(prefs.getBoolean("permission_asked",false) && !shouldShowRequestPermissionRationale(permission)) {
            new AlertDialog.Builder(this).setTitle("允许连接附近设备").setMessage("请在系统应用设置中允许蓝牙相关权限，再返回扫描。Android 11 及以下使用位置权限发现 BLE 设备。")
                .setNegativeButton("取消",null).setPositiveButton("前往设置",(d,w)->appSettings()).show();return;
        }
        prefs.edit().putBoolean("permission_asked",true).apply();
        requestPermissions(Build.VERSION.SDK_INT>=31?new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT}:new String[]{Manifest.permission.ACCESS_FINE_LOCATION,Manifest.permission.ACCESS_COARSE_LOCATION},1);
    }
    @Override public void onRequestPermissionsResult(int code,String[] permissions,int[] results) {
        super.onRequestPermissionsResult(code,permissions,results);
        if(code==1){if(bluetoothPermission()){if(pendingScan && deviceDialog!=null)beginScan();else if(page==2)render();}else{toast("未获得权限，可在设置中重新开启");if(scanHint!=null)scanHint.setText("需要蓝牙相关权限才能发现适配器。");}}
    }
    private void beginScan() {
        if(busy()||connecting()||deviceList==null)return;
        BluetoothManager manager=getSystemService(BluetoothManager.class);BluetoothAdapter adapter=manager==null?null:manager.getAdapter();
        if(adapter==null){scanMessage("此设备没有可用的蓝牙功能");return;}
        try {
            if(!adapter.isEnabled()){startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE),2);return;}
            if(Build.VERSION.SDK_INT<=30){LocationManager lm=getSystemService(LocationManager.class);boolean available=Build.VERSION.SDK_INT>=28?lm.isLocationEnabled():lm.isProviderEnabled(LocationManager.GPS_PROVIDER)||lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER);if(!available){new AlertDialog.Builder(this).setTitle("开启系统定位").setMessage("当前 Android 版本需要开启定位才能扫描 BLE 设备。采集 App 不读取位置。").setNegativeButton("取消",null).setPositiveButton("打开定位设置",(d,w)->startActivity(new Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS))).show();return;}}
            stopScan();scanner=adapter.getBluetoothLeScanner();if(scanner==null){scanMessage("蓝牙正在准备，请稍后重试");return;}
            seen.clear();deviceList.removeAllViews();scanning=true;scanner.startScan(null,new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),scanCallback);
            scanButton.setText("停止扫描");scanHint.setText(R.string.scanning);main.postDelayed(scanTimeout,15000);
        } catch(SecurityException|IllegalStateException e){stopScan();scanMessage("扫描不可用，请检查蓝牙开关与权限");}
    }
    private final ScanCallback scanCallback=new ScanCallback() {
        @Override public void onScanResult(int type,ScanResult result){main.post(()->{
            if(!scanning||deviceList==null||seen.size()>=40)return;
            try{String address=result.getDevice().getAddress();if(!seen.add(address))return;String advertised=result.getScanRecord()==null?null:result.getScanRecord().getDeviceName();if(advertised==null)advertised=result.getDevice().getName();final String name=advertised==null?"未命名 BLE 设备":advertised;
                detailRow(deviceList,"bluetooth",name,address+" · "+(result.getRssi()>-65?"信号强":result.getRssi()>-85?"信号一般":"信号较弱"),()->selectDevice(address,name));divider(deviceList);scanHint.setText(getString(R.string.scan_found,seen.size()));
            }catch(SecurityException e){stopScan();scanMessage("蓝牙权限已撤销，请在设置中重新允许");}
        });}
        @Override public void onScanFailed(int code){main.post(()->{stopScan();scanMessage("扫描未能启动（"+code+"），请稍后重试");});}
    };
    private void scanMessage(String message){if(scanHint!=null)scanHint.setText(message);else toast(message);}
    private void stopScan(){main.removeCallbacks(scanTimeout);boolean wasScanning=scanning;if(scanning&&scanner!=null)try{scanner.stopScan(scanCallback);}catch(SecurityException|IllegalStateException ignored){}scanning=false;if(scanButton!=null)scanButton.setText("重新扫描");if(wasScanning&&scanHint!=null)scanHint.setText(seen.isEmpty()?"未发现设备。请确认适配器已通电、未被其他设备连接。":"扫描完成 · "+seen.size()+" 个设备");}
    private void connectLast(){String address=prefs.getString("device_address","");if(address.isEmpty())showDevices();else selectDevice(address,prefs.getString("device_name","车辆适配器"));}
    private void selectDevice(String address,String name){
        if(busy()||connecting())return;if(!bluetoothPermission()){toast("请在设置中允许蓝牙权限");return;}stopScan();if(deviceDialog!=null)deviceDialog.dismiss();
        prefs.edit().putString("device_address",address).putString("device_name",name).apply();
        try{startForegroundService(new Intent(this,CaptureService.class).setAction(CaptureService.CONNECT).putExtra("address",address));openPage(0);}catch(SecurityException|IllegalStateException e){toast("连接未能启动，请检查系统权限后重试");}
    }
    private void startCapture(){if(service==null||!service.ready||busy())return;try{String normalized=mode==1?CaptureOptions.filter(filter,protocol+6):"";stopScan();service.startCapture(mode==1,protocol+6,normalized,CaptureOptions.SECONDS[duration],physical);}catch(IllegalArgumentException e){info("检查采集方案",e.getMessage());}}
    private File directory(){return new File(getFilesDir(),"captures");}
    private void refreshFiles(){
        if(readingFiles||isDestroyed())return;readingFiles=true;
        filesWorker.execute(()->{ArrayList<RecordSummary> result=new ArrayList<>();File[] list=directory().listFiles((d,n)->n.endsWith(".jsonl")||n.endsWith(".part"));if(list!=null)for(File f:list)result.add(RecordSummary.read(f));result.sort((a,b)->Long.compare(b.date,a.date));main.post(()->{readingFiles=false;if(isDestroyed())return;records.clear();records.addAll(result);if(page==1||page==2)render();});});
    }
    private String date(long ms){return new SimpleDateFormat("yyyy/MM/dd  HH:mm",Locale.getDefault()).format(new Date(ms));}
    private String formatBytes(long bytes){return bytes<1024?bytes+" B":bytes<1024*1024?String.format(Locale.getDefault(),"%.1f KB",bytes/1024.0):String.format(Locale.getDefault(),"%.1f MB",bytes/(1024.0*1024));}
    private void showRecord(RecordSummary r){
        LinearLayout body=column();body.setPadding(dp(24),dp(8),dp(24),dp(8));label(body,date(r.date),14,muted,false);gap(body,12);label(body,r.complete?"会话已正常结束":"记录需要检查",19,r.complete?accent:warning,true);gap(body,8);
        label(body,r.reasonText()+"\n"+r.adapter+"\n"+formatBytes(r.file.length())+(r.seconds>0?" · 计划 "+r.seconds+" 秒":""),14,ink,false);gap(body,12);
        label(body,r.monitor?"CAN 帧："+r.frames+"\n协议："+r.protocol+" · 过滤："+(r.filter.isEmpty()?"全部":r.filter):"接收通知："+r.notifications+"（包含初始化响应）",13,muted,false);
        gap(body,12);label(body,r.file.getName(),12,muted,false).setTextIsSelectable(true);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle(r.title()).setView(body).setNegativeButton("关闭",null).setNeutralButton("删除",null).setPositiveButton("导出文件",null).create();
        dialog.setOnShowListener(d->{dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{if(busy()){toast("请先停止采集再导出");return;}exportFile=r.file;Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/x-ndjson").putExtra(Intent.EXTRA_TITLE,r.file.getName());try{startActivityForResult(i,3);dialog.dismiss();}catch(ActivityNotFoundException e){exportFile=null;toast("系统文件选择器不可用");}});
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setTextColor(warning);dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v->{if(busy()){toast("请先停止采集");return;}new AlertDialog.Builder(this).setTitle("删除这份记录？").setMessage("删除后无法恢复。如需保留，请先导出。\n\n"+date(r.date)+" · "+r.title()).setNegativeButton("取消",null).setPositiveButton("删除",(d2,w)->{if(busy()){toast("采集中，未删除文件");return;}if(!r.file.delete())toast("删除失败，请稍后重试");dialog.dismiss();refreshFiles();}).show();});});dialog.show();
    }
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);
        if(request==2&&result==RESULT_OK&&deviceDialog!=null)beginScan();
        if(request==3){if(result==RESULT_OK&&data!=null&&data.getData()!=null&&exportFile!=null){File source=exportFile;Uri target=data.getData();filesWorker.execute(()->{try(InputStream in=new FileInputStream(source);OutputStream out=getContentResolver().openOutputStream(target,"w")){if(out==null)throw new IOException("导出位置不可写");byte[] bytes=new byte[8192];int n;while((n=in.read(bytes))!=-1)out.write(bytes,0,n);main.post(()->toast("记录已导出"));}catch(IOException|SecurityException e){main.post(()->toast("导出失败："+e.getMessage()));}});}exportFile=null;}
    }
    private void appSettings(){startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,Uri.parse("package:"+getPackageName())));}
    private void notificationSettings(){startActivity(new Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE,getPackageName()));}
    private void info(String title,String value){new AlertDialog.Builder(this).setTitle(title).setMessage(value).setPositiveButton("知道了",null).show();}
    private void toast(String value){Toast.makeText(this,value,Toast.LENGTH_LONG).show();}
}
