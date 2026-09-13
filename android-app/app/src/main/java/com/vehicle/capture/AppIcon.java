package com.vehicle.capture;

import android.content.Context;
import android.graphics.*;
import android.view.View;

/** Small vector icon set, rendered at the device density without font dependencies. */
final class AppIcon extends View {
    private final String kind;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    public AppIcon(Context context) { this(context,"capture",Color.GRAY); }
    AppIcon(Context context,String kind,int color) { super(context); this.kind=kind; paint.setColor(color); setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO); }
    private void line(Canvas c,float... points) { Path p=new Path(); p.moveTo(points[0],points[1]); for(int i=2;i<points.length;i+=2)p.lineTo(points[i],points[i+1]); c.drawPath(p,paint); }
    @Override protected void onDraw(Canvas c) {
        super.onDraw(c); c.save(); c.scale(getWidth()/24f,getHeight()/24f);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(1.7f); paint.setStrokeCap(Paint.Cap.ROUND); paint.setStrokeJoin(Paint.Join.ROUND);
        switch(kind) {
            case "settings": c.drawCircle(12,12,3,paint); c.drawCircle(12,12,7,paint); for(int i=0;i<8;i++){c.save();c.rotate(i*45,12,12);c.drawLine(12,2,12,5,paint);c.restore();} break;
            case "bluetooth": line(c,7,7,17,17,12,22,12,2,17,7,7,17); break;
            case "records": c.drawRoundRect(5,3,19,21,2,2,paint); line(c,9,8,15,8);line(c,9,12,15,12);line(c,9,16,13,16); break;
            case "capture": line(c,2,12,6,12,9,5,13,19,16,10,18,12,22,12);break;
            case "car": c.drawRoundRect(3,9,21,18,2,2,paint);line(c,5,9,7,4,17,4,19,9);line(c,6,18,6,21);line(c,18,18,18,21);line(c,6,13,8,13);line(c,16,13,18,13);break;
            case "arrow": line(c,9,5,16,12,9,19);break;
            case "clock":c.drawCircle(12,12,9,paint);line(c,12,6,12,12,16,14);break;
            case "sun":c.drawCircle(12,12,4,paint);for(int i=0;i<8;i++){c.save();c.rotate(i*45,12,12);c.drawLine(12,2,12,4,paint);c.restore();}break;
            case "shield":line(c,12,2,20,5,19,15,12,22,5,15,4,5,12,2);line(c,8,12,11,15,16,9);break;
            case "folder":line(c,3,8,3,5,10,5,12,8,21,8,21,20,3,20,3,8,21,8);break;
            default:c.drawCircle(12,12,9,paint);line(c,12,10,12,17);c.drawPoint(12,6,paint);break;
        }
        c.restore();
    }
}
