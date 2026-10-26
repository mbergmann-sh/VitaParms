// VitaParms Tablet – Kamera aufrufen und Dateien teilen (Aufruf aus C++ über JNI)
// SPDX-License-Identifier: MIT
package de.mbergmann.vitaparms;

import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;

import androidx.core.content.FileProvider;

import java.io.File;
import java.util.ArrayList;

public class Capture {
    // Foto aufnehmen: Ergebnis landet in 'path', die Markierung in 'path.done'
    public static boolean start(Context ctx, String path) {
        try {
            Intent i = new Intent(ctx, CaptureActivity.class);
            i.putExtra(CaptureActivity.EXTRA_PATH, path);
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(i);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    // Dateien über das Teilen-Menü von Android senden (E-Mail, Messenger, Cloud …)
    public static boolean share(Context ctx, String pathsJoined, String mime, String subject, String chooserTitle) {
        try {
            ArrayList<Uri> uris = new ArrayList<>();
            for (String p : pathsJoined.split("\n")) {
                if (p.isEmpty()) continue;
                uris.add(FileProvider.getUriForFile(ctx, ctx.getPackageName() + ".qtprovider", new File(p)));
            }
            if (uris.isEmpty()) return false;
            Intent send;
            if (uris.size() == 1) {
                send = new Intent(Intent.ACTION_SEND);
                send.putExtra(Intent.EXTRA_STREAM, uris.get(0));
            } else {
                send = new Intent(Intent.ACTION_SEND_MULTIPLE);
                send.putParcelableArrayListExtra(Intent.EXTRA_STREAM, uris);
            }
            ClipData clip = ClipData.newRawUri("", uris.get(0));
            for (int k = 1; k < uris.size(); ++k) clip.addItem(new ClipData.Item(uris.get(k)));
            send.setClipData(clip);
            send.setType(mime);
            send.putExtra(Intent.EXTRA_SUBJECT, subject);
            send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            Intent chooser = Intent.createChooser(send, chooserTitle);
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_GRANT_READ_URI_PERMISSION);
            ctx.startActivity(chooser);
            return true;
        } catch (Exception e) {
            return false;
        }
    }
}
