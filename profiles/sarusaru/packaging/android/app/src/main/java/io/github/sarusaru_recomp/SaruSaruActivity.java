package io.github.sarusaru_recomp;

import android.Manifest;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.KeyEvent;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

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
 * The game's memory-stick saves live in app-private storage; saveTransfer
 * copies them to a zip the player picks, or from a zip or a folder (such as
 * PPSSPP's PSP/SAVEDATA), as PSP save folders (host/platform/save_transfer.hpp).
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
    private static final int SAVES_EXPORT_REQUEST = 0x5a5b;
    private static final int SAVES_IMPORT_ARCHIVE_REQUEST = 0x5a5c;
    private static final int SAVES_IMPORT_FOLDER_REQUEST = 0x5a5d;
    // A PSP save folder's files are small; these bound what an import reads.
    private static final long MAX_SAVE_FILE = 32L * 1024 * 1024;
    private static final long MAX_IMPORT = 256L * 1024 * 1024;
    // How deep under a picked folder to look for save folders (PSP/SAVEDATA/<save>).
    private static final int MAX_FOLDER_DEPTH = 3;

    // The save transfer waiting for the picker (any thread; guarded by this).
    private File transferSaves;
    private String transferPrefix;
    private String[] transferTexts;

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
        if (requestCode == SAVES_EXPORT_REQUEST || requestCode == SAVES_IMPORT_ARCHIVE_REQUEST ||
            requestCode == SAVES_IMPORT_FOLDER_REQUEST) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) finishSaveTransfer(requestCode, data.getData());
            return;
        }
        if (resultCode == RESULT_OK && data != null) {
            keepReadable(data.getData());
            ClipData clip = data.getClipData();
            for (int i = 0; clip != null && i < clip.getItemCount(); ++i) keepReadable(clip.getItemAt(i).getUri());
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    /**
     * Called by the port through JNI (host/platform/save_transfer.cpp): opens
     * the picker for exporting (mode 0) the save folders in `directory` whose
     * names start with `prefix`, or importing them from a zip (1) or a folder
     * (2). `texts` are what to say when it ends: exported, imported, nothing
     * found and failed, each with {0} for a count or a reason.
     */
    public boolean saveTransfer(int mode, String directory, String prefix, String[] texts) {
        if (texts == null || texts.length != 4) return false;
        synchronized (this) {
            transferSaves = new File(directory);
            transferPrefix = prefix;
            transferTexts = texts.clone();
        }
        final Intent intent;
        final int request;
        if (mode == 0) {
            intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/zip");
            intent.putExtra(Intent.EXTRA_TITLE, prefix + "-saves.zip");
            request = SAVES_EXPORT_REQUEST;
        } else if (mode == 1) {
            intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_MIME_TYPES,
                new String[] {"application/zip", "application/x-zip-compressed", "application/octet-stream"});
            request = SAVES_IMPORT_ARCHIVE_REQUEST;
        } else if (mode == 2) {
            intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            request = SAVES_IMPORT_FOLDER_REQUEST;
        } else {
            return false;
        }
        runOnUiThread(() -> {
            try {
                startActivityForResult(intent, request);
            } catch (ActivityNotFoundException error) {
                Log.e(TAG, "no document picker", error);
                tell(3, error.toString());
            }
        });
        return true;
    }

    private void finishSaveTransfer(int request, Uri uri) {
        new Thread(() -> {
            try {
                if (request == SAVES_EXPORT_REQUEST) {
                    int count = exportSaves(uri);
                    tell(count > 0 ? 0 : 2, Integer.toString(count));
                } else {
                    Map<String, Map<String, byte[]>> folders = new TreeMap<>();
                    if (request == SAVES_IMPORT_ARCHIVE_REQUEST) readSaveArchive(uri, folders);
                    else readSaveTree(uri, folders);
                    int count = installSaves(folders);
                    tell(count > 0 ? 1 : 2, Integer.toString(count));
                }
            } catch (IOException | RuntimeException error) {
                Log.e(TAG, "save transfer failed", error);
                tell(3, error.getMessage() != null ? error.getMessage() : error.toString());
            }
        }, "save transfer").start();
    }

    /** Shows transferTexts[index] with {0} as `value`. */
    private void tell(int index, String value) {
        final String text;
        synchronized (this) {
            text = transferTexts != null ? transferTexts[index].replace("{0}", value) : value;
        }
        runOnUiThread(() -> Toast.makeText(this, text, Toast.LENGTH_LONG).show());
    }

    private boolean isSaveFolder(String name) {
        String prefix;
        synchronized (this) {
            prefix = transferPrefix;
        }
        return prefix != null && !prefix.isEmpty() && name.startsWith(prefix) && isPlainName(name);
    }

    /** A name for a save folder or file: letters, digits, '.', '_' or '-', not starting with '.'. */
    private static boolean isPlainName(String name) {
        if (name.isEmpty() || name.length() > 64 || name.startsWith(".")) return false;
        for (int i = 0; i < name.length(); ++i) {
            char c = name.charAt(i);
            if (!(Character.isLetterOrDigit(c) && c < 128) && c != '.' && c != '_' && c != '-') return false;
        }
        return true;
    }

    private File savesDirectory() {
        synchronized (this) {
            return transferSaves;
        }
    }

    /** Writes every save folder of the game into the zip at `uri`, as <folder>/<file>. */
    private int exportSaves(Uri uri) throws IOException {
        File[] folders = savesDirectory().listFiles();
        int count = 0;
        try (OutputStream out = getContentResolver().openOutputStream(uri, "wt");
             ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(out))) {
            if (folders != null) {
                Arrays.sort(folders);
                byte[] buffer = new byte[64 * 1024];
                for (File folder : folders) {
                    if (!folder.isDirectory() || !isSaveFolder(folder.getName())) continue;
                    File[] files = folder.listFiles();
                    if (files == null) continue;
                    Arrays.sort(files);
                    zip.putNextEntry(new ZipEntry(folder.getName() + "/"));
                    zip.closeEntry();
                    for (File file : files) {
                        if (!file.isFile()) continue;
                        zip.putNextEntry(new ZipEntry(folder.getName() + "/" + file.getName()));
                        try (InputStream in = new FileInputStream(file)) {
                            for (int length; (length = in.read(buffer)) > 0;) zip.write(buffer, 0, length);
                        }
                        zip.closeEntry();
                    }
                    ++count;
                }
            }
        }
        return count;
    }

    private static byte[] readBounded(InputStream in, long[] total) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        byte[] buffer = new byte[64 * 1024];
        for (int length; (length = in.read(buffer)) > 0;) {
            bytes.write(buffer, 0, length);
            total[0] += length;
            if (bytes.size() > MAX_SAVE_FILE || total[0] > MAX_IMPORT) throw new IOException("the saves are too large");
        }
        return bytes.toByteArray();
    }

    /** Save folders in a zip: any <folder>/<file> whose folder is the game's, at any depth (PSP/SAVEDATA/...). */
    private void readSaveArchive(Uri uri, Map<String, Map<String, byte[]>> folders) throws IOException {
        long[] total = {0};
        try (InputStream in = getContentResolver().openInputStream(uri);
             ZipInputStream zip = new ZipInputStream(new BufferedInputStream(in))) {
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null;) {
                if (entry.isDirectory()) continue;
                String[] parts = entry.getName().replace('\\', '/').split("/");
                if (parts.length < 2) continue;
                String folder = parts[parts.length - 2], name = parts[parts.length - 1];
                if (!isSaveFolder(folder) || !isPlainName(name)) continue;
                folders.computeIfAbsent(folder, key -> new TreeMap<>()).put(name, readBounded(zip, total));
            }
        }
    }

    /** Save folders in a picked folder: the folder itself, or ones up to MAX_FOLDER_DEPTH below it. */
    private void readSaveTree(Uri tree, Map<String, Map<String, byte[]>> folders) throws IOException {
        String root = DocumentsContract.getTreeDocumentId(tree);
        String rootName = root.substring(root.lastIndexOf('/') + 1);
        rootName = rootName.substring(rootName.lastIndexOf(':') + 1);
        long[] total = {0};
        if (isSaveFolder(rootName)) readSaveFolder(tree, root, rootName, folders, total);
        else searchTree(tree, root, 0, folders, total);
    }

    private void searchTree(Uri tree, String parent, int depth, Map<String, Map<String, byte[]>> folders, long[] total)
        throws IOException {
        for (String[] child : children(tree, parent)) {
            if (!DocumentsContract.Document.MIME_TYPE_DIR.equals(child[2])) continue;
            if (isSaveFolder(child[1])) readSaveFolder(tree, child[0], child[1], folders, total);
            else if (depth + 1 < MAX_FOLDER_DEPTH) searchTree(tree, child[0], depth + 1, folders, total);
        }
    }

    private void readSaveFolder(Uri tree, String id, String name, Map<String, Map<String, byte[]>> folders, long[] total)
        throws IOException {
        ContentResolver resolver = getContentResolver();
        for (String[] child : children(tree, id)) {
            if (DocumentsContract.Document.MIME_TYPE_DIR.equals(child[2]) || !isPlainName(child[1])) continue;
            try (InputStream in = resolver.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, child[0]))) {
                if (in == null) throw new IOException("cannot read " + child[1]);
                folders.computeIfAbsent(name, key -> new TreeMap<>()).put(child[1], readBounded(in, total));
            }
        }
    }

    /** The documents in a folder of the picked tree: {id, name, MIME type} each. */
    private java.util.List<String[]> children(Uri tree, String parent) {
        java.util.List<String[]> out = new java.util.ArrayList<>();
        Uri uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor cursor = getContentResolver().query(uri, columns, null, null, null)) {
            while (cursor != null && cursor.moveToNext() && out.size() < 1024)
                out.add(new String[] {cursor.getString(0), cursor.getString(1), cursor.getString(2)});
        }
        return out;
    }

    /**
     * Puts each save folder with a PARAM.SFO in place, replacing the game's
     * own of that name whole: written beside it first, then swapped in.
     */
    private int installSaves(Map<String, Map<String, byte[]>> folders) throws IOException {
        File saves = savesDirectory();
        if (!saves.isDirectory() && !saves.mkdirs()) throw new IOException("cannot create " + saves);
        int count = 0;
        for (Map.Entry<String, Map<String, byte[]>> folder : folders.entrySet()) {
            boolean hasParam = false;
            for (String name : folder.getValue().keySet()) hasParam |= name.equalsIgnoreCase("PARAM.SFO");
            if (!hasParam) continue;
            File staging = new File(saves, "." + folder.getKey() + ".import");
            deleteTree(staging);
            if (!staging.mkdirs()) throw new IOException("cannot create " + staging);
            for (Map.Entry<String, byte[]> file : folder.getValue().entrySet()) {
                try (OutputStream out = new FileOutputStream(new File(staging, file.getKey()))) {
                    out.write(file.getValue());
                }
            }
            File target = new File(saves, folder.getKey());
            deleteTree(target);
            if (!staging.renameTo(target)) throw new IOException("cannot replace " + target);
            ++count;
        }
        return count;
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
