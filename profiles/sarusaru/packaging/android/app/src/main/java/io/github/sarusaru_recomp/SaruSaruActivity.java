package io.github.sarusaru_recomp;

import android.Manifest;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.KeyEvent;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * SDL's activity around the port's libmain.so (host/shell/main.cpp).
 *
 * The port reads its shipped resources (fonts, licences) and the SDK the game
 * is built against (sdk.zip: the runtime's headers, the compiler's sysroot)
 * as files, which an APK's assets are not, so they are copied to app-private
 * storage once per installed version, and the copy is named to the port
 * through SARUSARU_SHIPPED_FILES (host/platform/paths.cpp).
 *
 * The disc image the player picks is a document URI, which the port opens
 * through SDL (host/platform/documents.hpp); the activity keeps the right to
 * read it, so the disc saved in the settings still opens after a restart.
 *
 * Installing the game goes on behind other apps: the launcher reports it
 * through backgroundWork (host/platform/background_work.cpp), which runs
 * InstallService for as long as it lasts.
 *
 * Command-line arguments come from the "args" string-array extra, for
 * development: adb shell am start -n io.github.sarusaru_recomp.SaruSaruRecomp/io.github.sarusaru_recomp.SaruSaruActivity --esa args --scale,2
 */
public class SaruSaruActivity extends SDLActivity {
    private static final String TAG = "sarusaru";
    private static final String ASSETS = "resources";
    private static final String SDK_ARCHIVE = "sdk.zip";
    // Not one of SDL's (SDL_RequestAndroidPermission counts its own up from 0).
    private static final int NOTIFICATIONS_REQUEST = 0x5a5a;

    // Whether the player is in the app, and was asked to allow notifications (main thread only).
    private boolean resumed;
    private boolean askedToNotify;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        try {
            File shipped = copyShippedFiles();
            Os.setenv("SARUSARU_SHIPPED_FILES", shipped.getPath(), true);
        } catch (IOException | ErrnoException | RuntimeException error) {
            // The launcher still runs, with ImGui's own font.
            Log.e(TAG, "cannot copy the shipped resources", error);
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        InstallService.clearResult(this);
    }

    @Override
    protected void onPause() {
        resumed = false;
        super.onPause();
    }

    @Override
    public void onBackPressed() {
        // System back gestures have no key event. Send the same pair SDL
        // delivers for the hardware Back button, instead of leaving the app.
        if (mBrokenLibraries) return;
        onNativeKeyDown(KeyEvent.KEYCODE_BACK);
        onNativeKeyUp(KeyEvent.KEYCODE_BACK);
    }

    /**
     * Called by the port through JNI, on any thread: long work started or
     * goes on (`ongoing`, `permille` -1 while the end is unknown), or ended
     * with `text` as its result.
     */
    public void backgroundWork(String title, String text, int permille, boolean ongoing) {
        if (ongoing) {
            runOnUiThread(this::askToNotify);
            InstallService.show(this, title, text, permille);
        } else {
            runOnUiThread(() -> InstallService.end(this, title, text, !resumed));
        }
    }

    /** The progress notification needs the player's leave from Android 13; the work goes on without it. */
    private void askToNotify() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return;
        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) return;
        if (askedToNotify) return;
        askedToNotify = true;
        requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, NOTIFICATIONS_REQUEST);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        if (requestCode == NOTIFICATIONS_REQUEST) return;  // not one SDL is waiting for
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (resultCode == RESULT_OK && data != null) {
            keepReadable(data.getData());
            ClipData clip = data.getClipData();
            for (int i = 0; clip != null && i < clip.getItemCount(); ++i) keepReadable(clip.getItemAt(i).getUri());
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    private void keepReadable(Uri uri) {
        if (uri == null) return;
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException error) {
            // The provider offers no lasting access: readable until the app closes.
            Log.w(TAG, "cannot keep access to " + uri, error);
        }
    }

    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        // The PSP's screen is landscape, either way up and whatever the
        // rotation lock says. SDL would choose from the window and the hint,
        // which allows portrait for a resizable window on some devices.
        Log.i(TAG, "requesting landscape (SDL asked for " + w + "x" + h + ", hint " + hint + ")");
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected String[] getLibraries() {
        // libc++_shared, the runtime and FFmpeg load as libmain.so's dependencies.
        return new String[] {"SDL3", "main"};
    }

    @Override
    protected String[] getArguments() {
        String[] arguments = getIntent().getStringArrayExtra("args");
        return arguments != null ? arguments : new String[0];
    }

    /** no_backup/shipped, holding resources/ as in the APK's assets, for this installed version. */
    private File copyShippedFiles() throws IOException {
        File shipped = new File(getNoBackupFilesDir(), "shipped");
        File stamp = new File(shipped, ".version");
        String version = installedVersion();
        if (stamp.isFile() && version.equals(new String(Files.readAllBytes(stamp.toPath()), StandardCharsets.UTF_8)))
            return shipped;
        deleteTree(shipped);
        copyAssets(getAssets(), ASSETS, new File(shipped, ASSETS));
        unzipAsset(getAssets(), SDK_ARCHIVE, shipped);
        try (OutputStream out = new FileOutputStream(stamp)) {
            out.write(version.getBytes(StandardCharsets.UTF_8));
        }
        return shipped;
    }

    private String installedVersion() {
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            return info.getLongVersionCode() + " " + info.lastUpdateTime;
        } catch (android.content.pm.PackageManager.NameNotFoundException error) {
            throw new IllegalStateException(error);
        }
    }

    private static void copyAssets(AssetManager assets, String path, File target) throws IOException {
        String[] children = assets.list(path);
        if (children != null && children.length > 0) {
            if (!target.isDirectory() && !target.mkdirs()) throw new IOException("cannot create " + target);
            for (String child : children) copyAssets(assets, path + "/" + child, new File(target, child));
            return;
        }
        // A file (or an empty directory, which assets do not keep).
        try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
            byte[] buffer = new byte[64 * 1024];
            for (int length; (length = in.read(buffer)) > 0;) out.write(buffer, 0, length);
        }
    }

    /** Unpacks an archive among the assets into `target`, if the APK has it. */
    private static void unzipAsset(AssetManager assets, String name, File target) throws IOException {
        InputStream source;
        try {
            source = assets.open(name);
        } catch (IOException missing) {
            return;  // an APK without the compiler
        }
        String root = target.getCanonicalPath() + File.separator;
        byte[] buffer = new byte[64 * 1024];
        try (ZipInputStream zip = new ZipInputStream(source)) {
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null;) {
                File file = new File(target, entry.getName());
                if (!file.getCanonicalPath().startsWith(root)) throw new IOException("bad entry " + entry.getName());
                if (entry.isDirectory()) {
                    if (!file.isDirectory() && !file.mkdirs()) throw new IOException("cannot create " + file);
                    continue;
                }
                File parent = file.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs())
                    throw new IOException("cannot create " + parent);
                try (OutputStream out = new FileOutputStream(file)) {
                    for (int length; (length = zip.read(buffer)) > 0;) out.write(buffer, 0, length);
                }
            }
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteTree(child);
        }
        if (file.exists() && !file.delete()) Log.w(TAG, "cannot delete " + file);
    }
}
