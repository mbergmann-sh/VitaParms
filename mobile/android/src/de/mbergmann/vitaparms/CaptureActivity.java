// VitaParms Tablet – Foto für die Krankenakte mit der Kamera-App des Geräts aufnehmen
//
// Die App (C++) ruft Capture.start() mit einem Zielpfad im Cache-Ordner auf. Diese unsichtbare
// Activity öffnet die Kamera-App (ACTION_IMAGE_CAPTURE, Ausgabe über den FileProvider der App)
// und schreibt nach der Rückkehr eine Markierungsdatei "<Ziel>.done" mit "ok" oder "cancel".
// Die App prüft das, sobald sie wieder im Vordergrund ist. So braucht VitaParms weder eine
// eigene Kamera-Berechtigung noch private Qt-Schnittstellen.
// SPDX-License-Identifier: MIT
package de.mbergmann.vitaparms;

import android.Manifest;
import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.provider.MediaStore;

import androidx.core.content.FileProvider;

import java.io.File;
import java.io.FileOutputStream;

public class CaptureActivity extends Activity {
    static final String EXTRA_PATH = "de.mbergmann.vitaparms.capturePath";
    private static final int REQUEST = 4711;
    private String m_path;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        m_path = state != null ? state.getString(EXTRA_PATH) : getIntent().getStringExtra(EXTRA_PATH);
        if (state != null) return;                  // nach Neuaufbau wartet die Activity auf das Ergebnis
        // Steht CAMERA im Manifest (Qt Multimedia kann es eintragen), verweigert Android den Aufruf der
        // Kamera-App, solange die Berechtigung nicht erteilt ist – dann zuerst danach fragen.
        if (declaresCamera() && checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] { Manifest.permission.CAMERA }, REQUEST + 1);
            return;
        }
        launch();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        if (requestCode != REQUEST + 1) return;
        if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) launch();
        else done("error Kamera-Berechtigung verweigert");
    }

    private boolean declaresCamera() {
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), PackageManager.GET_PERMISSIONS);
            if (pi.requestedPermissions != null)
                for (String p : pi.requestedPermissions)
                    if (Manifest.permission.CAMERA.equals(p)) return true;
        } catch (Exception ignored) {
        }
        return false;
    }

    private void launch() {
        try {
            File out = new File(m_path);
            Uri uri = FileProvider.getUriForFile(this, getPackageName() + ".qtprovider", out);
            Intent cam = new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
            cam.putExtra(MediaStore.EXTRA_OUTPUT, uri);
            cam.setClipData(ClipData.newRawUri("", uri));
            cam.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(cam, REQUEST);
        } catch (ActivityNotFoundException e) {
            done("error keine Kamera-App");
        } catch (Exception e) {
            done("error " + e.getClass().getSimpleName() + ": " + e.getMessage());
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putString(EXTRA_PATH, m_path);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST) done(resultCode == RESULT_OK ? "ok" : "cancel");
    }

    private void done(String result) {
        try (FileOutputStream f = new FileOutputStream(m_path + ".done")) {
            f.write(result.getBytes("UTF-8"));
        } catch (Exception ignored) {
        }
        finish();
    }
}
