// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.utils;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;
import java.lang.ref.WeakReference;

/** Displays a specific file failure after the native loader has stopped. Called through JNI. */
public final class Nx2LoadError {
    private Nx2LoadError() {}

    public static boolean show(WeakReference<? extends Activity> reference, String details) {
        final Activity activity = reference == null ? null : reference.get();
        if (activity == null || activity.isFinishing() || activity.isDestroyed()) return false;
        final boolean portuguese = activity.getResources().getConfiguration()
                .getLocales().get(0).getLanguage().equals("pt");
        return new Handler(Looper.getMainLooper()).post(() -> {
            if (activity.isFinishing() || activity.isDestroyed()) return;
            final AlertDialog dialog = new AlertDialog.Builder(activity)
                    .setTitle(portuguese ? "Erro nos arquivos do jogo" : "Game file error")
                    .setMessage((portuguese
                            ? "Confira os arquivos da mesma versão do jogo.\n\n"
                            : "Check the files for the selected game version.\n\n") + details)
                    .setPositiveButton(android.R.string.ok, (buttonDialog, which) -> activity.finish())
                    .setNeutralButton(portuguese ? "Copiar detalhes" : "Copy details", null)
                    .setOnDismissListener(dismissed -> activity.finish())
                    .create();
            dialog.show();
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(view -> {
                final ClipboardManager clipboard = activity.getSystemService(ClipboardManager.class);
                if (clipboard == null) return;
                clipboard.setPrimaryClip(ClipData.newPlainText("Eden EXP04", details));
                Toast.makeText(activity, portuguese ? "Detalhes copiados" : "Details copied",
                        Toast.LENGTH_SHORT).show();
            });
        });
    }
}
