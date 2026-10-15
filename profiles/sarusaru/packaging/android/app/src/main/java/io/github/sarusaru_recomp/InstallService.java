package io.github.sarusaru_recomp;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

/**
 * Keeps the process running while the launcher installs the game, which takes
 * minutes of compiling (host/install/game_builder.cpp): an app behind other
 * apps may otherwise be stopped or killed partway. A foreground service of the
 * dataSync type (local file processing), showing the progress the launcher
 * reports through SaruSaruActivity.backgroundWork.
 *
 * All state changes happen on the main thread, as the service's own
 * callbacks do, so a start and an end in quick succession stay in order.
 */
public class InstallService extends Service {
    private static final String TAG = "sarusaru";
    private static final String CHANNEL = "install";
    private static final int PROGRESS_ID = 1;
    private static final int RESULT_ID = 2;
    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    // The work's latest state (main thread only).
    private static boolean wanted;
    private static String title = "";
    private static String text = "";
    private static int permille = -1;
    private static InstallService running;

    private int lastStartId;

    /** Starts the service, or updates its notification. `permille` is -1 while the end is unknown. */
    static void show(Context context, String title, String text, int permille) {
        Context app = context.getApplicationContext();
        MAIN.post(() -> {
            InstallService.title = title;
            InstallService.text = text;
            InstallService.permille = permille;
            if (wanted && running != null) {
                notifications(app).notify(PROGRESS_ID, progress(app));
                return;
            }
            if (wanted) return;  // starting: onStartCommand shows the latest
            wanted = true;
            notifications(app).cancel(RESULT_ID);
            try {
                app.startForegroundService(new Intent(app, InstallService.class));
            } catch (IllegalStateException error) {
                // Not allowed from the background (ForegroundServiceStartNotAllowedException):
                // the installation carries on while the app stays in front.
                wanted = false;
                Log.w(TAG, "cannot start the installation's service", error);
            }
        });
    }

    /** Stops the service. A non-empty `result` stays as a notification when `notify` is set. */
    static void end(Context context, String title, String result, boolean notify) {
        Context app = context.getApplicationContext();
        MAIN.post(() -> {
            wanted = false;
            if (running != null) running.finish();
            // Otherwise onStartCommand, still to come, finishes at once.
            if (notify && !result.isEmpty()) notifications(app).notify(RESULT_ID, result(app, title, result));
        });
    }

    /** Clears the result of an installation, once the player is back in the app. */
    static void clearResult(Context context) {
        notifications(context.getApplicationContext()).cancel(RESULT_ID);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        running = this;
        lastStartId = startId;
        // Required within seconds of startForegroundService, even if the work already ended.
        startForeground(PROGRESS_ID, progress(this), ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        if (!wanted) finish();
        // A killed installation is started again by the player, not by the system.
        return START_NOT_STICKY;
    }

    @Override
    public void onTimeout(int startId, int foregroundServiceType) {
        // Android 15 limits dataSync services to hours; an installation takes minutes.
        Log.w(TAG, "the installation's service ran out of time");
        finish();
    }

    @Override
    public void onDestroy() {
        if (running == this) running = null;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void finish() {
        stopForeground(STOP_FOREGROUND_REMOVE);
        // A start that came after this one keeps the service.
        stopSelfResult(lastStartId);
    }

    private static NotificationManager notifications(Context context) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager.getNotificationChannel(CHANNEL) == null) {
            NotificationChannel channel = new NotificationChannel(CHANNEL, context.getString(R.string.install_channel),
                NotificationManager.IMPORTANCE_LOW);
            channel.setShowBadge(false);
            manager.createNotificationChannel(channel);
        }
        return manager;
    }

    private static Notification.Builder builder(Context context) {
        Intent open = new Intent(context, SaruSaruActivity.class)
            .setAction(Intent.ACTION_MAIN)
            .addCategory(Intent.CATEGORY_LAUNCHER);
        notifications(context);  // the channel exists
        return new Notification.Builder(context, CHANNEL)
            .setSmallIcon(R.drawable.ic_install)
            .setContentIntent(PendingIntent.getActivity(context, 0, open,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT));
    }

    private static Notification progress(Context context) {
        Notification.Builder builder = builder(context)
            .setContentTitle(title)
            .setContentText(text)
            .setCategory(Notification.CATEGORY_PROGRESS)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setProgress(1000, Math.max(permille, 0), permille < 0);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            builder.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        return builder.build();
    }

    private static Notification result(Context context, String title, String result) {
        return builder(context)
            .setContentTitle(title)
            .setContentText(result)
            .setAutoCancel(true)
            .build();
    }
}
