// VitaParms mobile – Symbole der Status- und Navigationsleiste passend zum Design
// (Android 15+ zeichnet die App bis unter die Leisten; ohne Anpassung stünden im dunklen Design
// dunkle Symbole auf dunklem Grund)
// SPDX-License-Identifier: MIT
package de.mbergmann.vitaparms;

import android.app.Activity;
import android.content.Context;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowInsetsController;

public class SystemBars {
    // light = true: heller Hintergrund → dunkle Symbole
    public static void setLight(Context ctx, final boolean light) {
        if (!(ctx instanceof Activity)) return;
        final Activity a = (Activity) ctx;
        a.runOnUiThread(() -> apply(a, light));
    }

    @SuppressWarnings("deprecation")
    private static void apply(Activity a, boolean light) {
        Window w = a.getWindow();
        if (w == null) return;
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = w.getInsetsController();
            if (c == null) return;
            int mask = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                     | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
            c.setSystemBarsAppearance(light ? mask : 0, mask);
        } else {
            View d = w.getDecorView();
            int f = d.getSystemUiVisibility();
            int bits = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
            d.setSystemUiVisibility(light ? (f | bits) : (f & ~bits));
        }
    }
}
