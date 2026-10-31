// VitaParms Tablet – Termin-Erinnerungen als Android-Benachrichtigung
//
// Die App (C++) stellt für die nächsten Erinnerungen je einen Wecker (AlarmManager); zur Zeit
// zeigt ReminderReceiver die Benachrichtigung – auch wenn VitaParms im Hintergrund ist oder
// beendet wurde. Die gestellten Wecker stehen zusätzlich in den (app-eigenen) SharedPreferences,
// damit BootReceiver sie nach einem Neustart des Tablets wieder stellen kann.
// SPDX-License-Identifier: MIT
package de.mbergmann.vitaparms;

import android.app.Activity;
import android.app.AlarmManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;

import org.json.JSONArray;
import org.json.JSONObject;

public class Reminders {
    static final String CHANNEL_SOUND = "termine";
    static final String CHANNEL_SILENT = "termine_still";
    static final String PREFS = "vitaparms_reminders";
    static final String KEY_PLAN = "plan";

    // Kanäle anlegen bzw. umbenennen (Texte in der Programmsprache)
    public static void ensureChannels(Context ctx, String nameSound, String nameSilent, String description) {
        if (Build.VERSION.SDK_INT < 26) return;
        NotificationManager nm = (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        NotificationChannel a = new NotificationChannel(CHANNEL_SOUND, nameSound, NotificationManager.IMPORTANCE_HIGH);
        a.setDescription(description);
        a.setLockscreenVisibility(Notification.VISIBILITY_PRIVATE);
        nm.createNotificationChannel(a);
        NotificationChannel b = new NotificationChannel(CHANNEL_SILENT, nameSilent, NotificationManager.IMPORTANCE_DEFAULT);
        b.setDescription(description);
        b.setSound(null, null);
        b.enableVibration(false);
        b.setLockscreenVisibility(Notification.VISIBILITY_PRIVATE);
        nm.createNotificationChannel(b);
    }

    // Android 13+: Erlaubnis für Benachrichtigungen erfragen (einmalig; danach in den Systemeinstellungen)
    public static boolean ensurePermission(Context ctx) {
        if (Build.VERSION.SDK_INT < 33) return true;
        if (ctx.checkSelfPermission("android.permission.POST_NOTIFICATIONS") == PackageManager.PERMISSION_GRANTED) return true;
        if (ctx instanceof Activity)
            ((Activity) ctx).requestPermissions(new String[] { "android.permission.POST_NOTIFICATIONS" }, 4711);
        return false;
    }

    public static boolean permissionGranted(Context ctx) {
        if (Build.VERSION.SDK_INT < 33) return true;
        return ctx.checkSelfPermission("android.permission.POST_NOTIFICATIONS") == PackageManager.PERMISSION_GRANTED;
    }

    private static PendingIntent alarmIntent(Context ctx, int id, String title, String text, String publicText, boolean sound) {
        Intent i = new Intent(ctx, ReminderReceiver.class);
        i.setAction("de.mbergmann.vitaparms.REMINDER." + id);
        i.putExtra("id", id);
        i.putExtra("title", title);
        i.putExtra("text", text);
        i.putExtra("publicText", publicText);
        i.putExtra("sound", sound);
        return PendingIntent.getBroadcast(ctx, id, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private static void setAlarm(Context ctx, int id, long atMs, String title, String text, String publicText, boolean sound) {
        AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
        PendingIntent pi = alarmIntent(ctx, id, title, text, publicText, sound);
        boolean exact = Build.VERSION.SDK_INT < 31 || am.canScheduleExactAlarms();
        try {
            if (exact) am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, atMs, pi);
            else am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, atMs, pi);
        } catch (SecurityException e) {
            am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, atMs, pi);
        }
    }

    // Alle bisher gestellten Wecker durch 'planJson' ersetzen:
    // [{"id":1,"at":1730000000000,"title":"…","text":"…","sound":true}, …]
    public static void replaceAll(Context ctx, String planJson, String publicText) {
        SharedPreferences prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
        try {
            JSONArray old = new JSONArray(prefs.getString(KEY_PLAN, "[]"));
            for (int k = 0; k < old.length(); ++k) {
                JSONObject o = old.getJSONObject(k);
                am.cancel(alarmIntent(ctx, o.getInt("id"), "", "", "", false));
            }
        } catch (Exception e) { /* alter Plan unlesbar: nichts zu streichen */ }
        JSONArray kept = new JSONArray();
        long now = System.currentTimeMillis();
        try {
            JSONArray plan = new JSONArray(planJson);
            for (int k = 0; k < plan.length(); ++k) {
                JSONObject o = plan.getJSONObject(k);
                long at = o.getLong("at");
                if (at <= now) continue;
                o.put("publicText", publicText);
                setAlarm(ctx, o.getInt("id"), at, o.getString("title"), o.getString("text"), publicText, o.optBoolean("sound", true));
                kept.put(o);
            }
        } catch (Exception e) { /* ungültiger Plan */ }
        prefs.edit().putString(KEY_PLAN, kept.toString()).apply();
    }

    // nach einem Neustart: gespeicherten Plan erneut stellen
    static void restore(Context ctx) {
        SharedPreferences prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        try {
            JSONArray plan = new JSONArray(prefs.getString(KEY_PLAN, "[]"));
            long now = System.currentTimeMillis();
            for (int k = 0; k < plan.length(); ++k) {
                JSONObject o = plan.getJSONObject(k);
                if (o.getLong("at") <= now) continue;
                setAlarm(ctx, o.getInt("id"), o.getLong("at"), o.getString("title"), o.getString("text"),
                         o.optString("publicText", "VitaParms"), o.optBoolean("sound", true));
            }
        } catch (Exception e) { /* nichts zu tun */ }
    }

    static void show(Context ctx, int id, String title, String text, String publicText, boolean sound) {
        if (!permissionGranted(ctx)) return;
        NotificationManager nm = (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        Intent open = ctx.getPackageManager().getLaunchIntentForPackage(ctx.getPackageName());
        PendingIntent content = open == null ? null
            : PendingIntent.getActivity(ctx, 0, open, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        String channel = sound ? CHANNEL_SOUND : CHANNEL_SILENT;
        int icon = ctx.getResources().getIdentifier("ic_stat_vitaparms", "drawable", ctx.getPackageName());
        if (icon == 0) icon = android.R.drawable.ic_popup_reminder;
        // Sperrbildschirm: nur „VitaParms – Termin-Erinnerung“, keine Einzelheiten
        Notification pub = new Notification.Builder(ctx, channel)
            .setSmallIcon(icon).setContentTitle("VitaParms").setContentText(publicText).build();
        Notification.Builder b = new Notification.Builder(ctx, channel)
            .setSmallIcon(icon)
            .setContentTitle(title)
            .setContentText(text)
            .setStyle(new Notification.BigTextStyle().bigText(text))
            .setCategory(Notification.CATEGORY_REMINDER)
            .setVisibility(Notification.VISIBILITY_PRIVATE)
            .setPublicVersion(pub)
            .setAutoCancel(true);
        if (content != null) b.setContentIntent(content);
        nm.notify(id, b.build());
    }
}
