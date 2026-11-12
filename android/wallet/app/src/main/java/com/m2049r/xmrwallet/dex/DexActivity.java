package com.m2049r.xmrwallet.dex;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.widget.SwitchCompat;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.m2049r.xmrwallet.R;
import com.m2049r.xmrwallet.SecureActivity;

import org.json.JSONObject;

/**
 * The wallet's DEX screen: switch the link to the DEX web page on and off, and see what it is
 * doing. There is no trading screen in the wallet; trading is on the page, in the phone's browser.
 * Opened from an open wallet, it also remembers that wallet's address as the place trading XMZ is
 * moved back to.
 */
public class DexActivity extends SecureActivity {
    public static final String EXTRA_HOME = "home", EXTRA_DAEMON = "daemon";
    private SwitchCompat link;
    private TextView status;
    private Button pending, back, words, disconnect;
    private final Runnable refresh = this::refresh;

    /** From an open wallet: its address is where trading XMZ goes back to, its node the one the trading wallet uses. */
    public static void open(Context context, String homeAddress, String daemon) {
        context.startActivity(new Intent(context, DexActivity.class).putExtra(EXTRA_HOME, homeAddress).putExtra(EXTRA_DAEMON, daemon));
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_dex);
        final String home = getIntent().getStringExtra(EXTRA_HOME), daemon = getIntent().getStringExtra(EXTRA_DAEMON);
        if (home != null && home.matches("[0-9A-Za-z]{90,110}")) DexService.prefs(this).edit().putString(DexService.PREF_HOME, home).apply();
        if (DexConfig.daemonAddress(daemon)) DexService.prefs(this).edit().putString(DexService.PREF_DAEMON, daemon).apply();
        if (com.m2049r.xmrwallet.BuildConfig.DEBUG && getIntent().getBooleanExtra("selfcheck", false))
            new Thread(() -> DexSelfCheck.run(getApplicationContext()), "dex-selfcheck").start();
        link = findViewById(R.id.dexSwitch);
        status = findViewById(R.id.dexStatus);
        pending = findViewById(R.id.dexPending);
        back = findViewById(R.id.dexReturn);
        words = findViewById(R.id.dexWords);
        disconnect = findViewById(R.id.dexDisconnect);
        link.setOnClickListener(v -> {
            if (link.isChecked()) {
                if (Build.VERSION.SDK_INT >= 33 && ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                    Toast.makeText(this, R.string.dex_notifications_needed, Toast.LENGTH_LONG).show();
                    ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.POST_NOTIFICATIONS}, 1);
                }
                DexService.start(this);
            } else if (!DexService.stop(this)) {
                Toast.makeText(this, R.string.dex_stop_refused_text, Toast.LENGTH_LONG).show();
            }
            status.postDelayed(refresh, 300);
        });
        findViewById(R.id.dexOpenPage).setOnClickListener(v -> {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://monzero.org/dex/")));
            } catch (Exception ignored) {
            }
        });
        pending.setOnClickListener(v -> startActivity(new Intent(this, DexPromptActivity.class)));
        back.setOnClickListener(v -> {
            final DexService s = DexService.get();
            if (s == null) return;
            s.engine().post(() -> {
                s.engine().refreshWallet();
                s.bridge().askReturn();
            });
            startActivity(new Intent(this, DexPromptActivity.class));
        });
        disconnect.setOnClickListener(v -> {
            final DexService s = DexService.get();
            if (s != null) s.engine().post(() -> s.bridge().disconnectPage());
        });
        words.setOnClickListener(v -> askWords());
    }

    @Override
    protected void onResume() {
        super.onResume();
        DexService.addUiListener(refresh);
        refresh();
    }

    @Override
    protected void onPause() {
        DexService.removeUiListener(refresh);
        super.onPause();
    }

    private void askWords() {
        final DexService s = DexService.get();
        if (s == null) return;
        final EditText input = new EditText(this);
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        input.setHint(R.string.dex_password);
        new AlertDialog.Builder(this).setTitle(R.string.dex_words).setView(input)
                .setNegativeButton(R.string.dex_close, null)
                .setPositiveButton(R.string.dex_keys_unlock, (d, which) -> {
                    final String typed = input.getText().toString();
                    new Thread(() -> {
                        final String found = s.engine().recoveryWords(typed);
                        runOnUiThread(() -> {
                            if (isFinishing()) return;
                            new AlertDialog.Builder(this).setTitle(R.string.dex_words_title)
                                    .setMessage(found == null ? getString(R.string.dex_words_wrong) : getString(R.string.dex_words_body, DexPromptActivity.numbered(found)))
                                    .setPositiveButton(R.string.dex_close, null).show();
                        });
                    }, "dex-words").start();
                }).show();
    }

    private void refresh() {
        if (isFinishing()) return;
        final DexService s = DexService.get();
        final boolean on = s != null && s.engine() != null;
        link.setChecked(on);
        back.setVisibility(on ? View.VISIBLE : View.GONE);
        words.setVisibility(View.GONE);
        disconnect.setVisibility(View.GONE);
        pending.setVisibility(View.GONE);
        final String home = DexService.prefs(this).getString(DexService.PREF_HOME, "");
        final String homeLine = home.isEmpty() ? getString(R.string.dex_status_home_none) : getString(R.string.dex_status_home, shorten(home));
        if (!on) {
            status.setText(getString(R.string.dex_status_off) + "\n" + homeLine);
            return;
        }
        final DexEngine engine = s.engine();
        final DexBridge bridge = s.bridge();
        final String[] lines = engine.call(() -> {
            final JSONObject wallet = engine.walletStatus();
            final JSONObject trade = engine.tradeStatus();
            final JSONObject snapshot = trade.optJSONObject("snapshot");
            final StringBuilder text = new StringBuilder();
            text.append(getString(R.string.dex_status_link, !s.listening() ? getString(R.string.dex_link_no_port)
                    : bridge.connected() ? getString(R.string.dex_status_page, bridge.origin()) : getString(R.string.dex_status_page_none))).append('\n');
            final String state = wallet.optString("state");
            text.append(getString(R.string.dex_status_wallet, state + (wallet.optString("error").isEmpty() ? "" : " (" + wallet.optString("error") + ")"))).append('\n');
            if (state.equals("ready")) text.append(getString(R.string.dex_status_balance, wallet.optString("balance"), wallet.optString("unlocked"))).append('\n');
            text.append(getString(!wallet.optBoolean("keysExist") ? R.string.dex_status_keys_none
                    : wallet.optBoolean("keysReady") ? R.string.dex_status_keys_ready : R.string.dex_status_keys_locked)).append('\n');
            if (!wallet.optString("tradeAccount").isEmpty()) text.append(getString(R.string.dex_status_account, wallet.optString("tradeAccount"))).append('\n');
            if (trade.optBoolean("configured") && snapshot != null && snapshot.has("stage"))
                text.append(getString(R.string.dex_status_trade, DexText.stage(this, snapshot))).append('\n');
            final String limits = engine.limitsText();
            if (!limits.isEmpty()) text.append(getString(R.string.dex_status_limits)).append('\n').append(limits).append('\n');
            final boolean waiting = bridge.pairing() != null || (bridge.keysWanted() && !engine.keysReady()) || engine.awaitingApproval() || bridge.returnWanted();
            return new String[]{text.toString(), waiting ? "1" : "", bridge.connected() ? "1" : "", wallet.optBoolean("keysExist") ? "1" : ""};
        }, null);
        if (lines == null) return;
        status.setText(lines[0] + homeLine);
        pending.setVisibility(lines[1].isEmpty() ? View.GONE : View.VISIBLE);
        disconnect.setVisibility(lines[2].isEmpty() ? View.GONE : View.VISIBLE);
        words.setVisibility(lines[3].isEmpty() ? View.GONE : View.VISIBLE);
    }

    private static String shorten(String address) {
        return address.length() < 24 ? address : address.substring(0, 10) + "…" + address.substring(address.length() - 10);
    }
}
