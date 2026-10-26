// VitaParms Tablet – Foto für die Krankenakte mit der Kamera-App des Geräts aufnehmen
//
// Die App (C++) ruft Capture.start() mit einem Zielpfad im Cache-Ordner auf. Diese unsichtbare
// Activity öffnet die Kamera-App (ACTION_IMAGE_CAPTURE, Ausgabe über den FileProvider der App)
// und schreibt nach der Rückkehr eine Markierungsdatei "<Ziel>.done" mit "ok" oder "cancel".
// Die App prüft das, sobald sie wieder im Vordergrund ist. So braucht VitaParms weder eine
// eigene Kamera-Berechtigung noch private Qt-Schnittstellen.
// SPDX-License-Identifier: MIT
package de.mbergmann.vitaparms;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
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
        try {
            File out = new File(m_path);
            Uri uri = FileProvider.getUriForFile(this, getPackageName() + ".qtprovider", out);
            Intent cam = new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
            cam.putExtra(MediaStore.EXTRA_OUTPUT, uri);
            cam.setClipData(ClipData.newRawUri("", uri));
            cam.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(cam, REQUEST);
        } catch (Exception e) {
            done("error " + e.getClass().getSimpleName());
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
