package io.github.sarusaru_recomp;

import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * SDL's activity around the port's libmain.so (host/shell/main.cpp).
 *
 * The port reads its shipped resources (fonts, licences) as files, which an
 * APK's assets are not, so they are copied to app-private storage once per
 * installed version, and the copy is named to the port through
 * SARUSARU_SHIPPED_FILES (host/platform/paths.cpp).
 *
 * Command-line arguments come from the "args" string-array extra, for
 * development: adb shell am start -n io.github.sarusaru_recomp.SaruSaruRecomp/io.github.sarusaru_recomp.SaruSaruActivity --esa args --scale,2
 */
public class SaruSaruActivity extends SDLActivity {
    private static final String TAG = "sarusaru";
    private static final String ASSETS = "resources";

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

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteTree(child);
        }
        if (file.exists() && !file.delete()) Log.w(TAG, "cannot delete " + file);
    }
}
