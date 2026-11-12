package com.m2049r.xmrwallet.dex;

import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

import com.m2049r.xmrwallet.R;
import com.m2049r.xmrwallet.SecureActivity;

import org.json.JSONObject;

/**
 * The wallet's side of everything the DEX page needs from the user, opened from a notification:
 * accept a connection (compare the code), enter the trading password, approve a trade, move XMZ
 * back. One request at a time, the most pressing first. The amounts of a trade are the ones this
 * wallet holds from the signed offer, never what a page says.
 */
public class DexPromptActivity extends SecureActivity {
    private TextView title, code, body, note;
    private EditText password, repeat;
    private Button yes, later, no;
    private String showing = "", notice = "";
    private boolean askedFresh, returnAsked, clearOnFail;
    private String wordsFor;        // set while the recovery words are on screen
    private String wordsPassword;   // the password just used to make fresh keys: its words are shown once
    private final Runnable refresh = this::refresh;

    /** What is waiting, read in one go on the engine's thread. */
    private static final class Waiting {
        DexBridge.Pairing pairing;
        boolean keysWanted, keysExist, keysReady, keysBusy, approval, returnWanted, walletBusy;
        String keysMessage = "", commitment = "", startRefusal = "", unlocked = "0", payoutAccount = "", walletError = "";
        JSONObject snapshot;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_dex_prompt);
        title = findViewById(R.id.dexPromptTitle);
        code = findViewById(R.id.dexPromptCode);
        body = findViewById(R.id.dexPromptBody);
        note = findViewById(R.id.dexPromptNote);
        password = findViewById(R.id.dexPromptPassword);
        repeat = findViewById(R.id.dexPromptRepeat);
        yes = findViewById(R.id.dexPromptYes);
        later = findViewById(R.id.dexPromptLater);
        no = findViewById(R.id.dexPromptNo);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        notice = "";
        refresh();
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

    private Waiting waiting() {
        final DexService service = DexService.get();
        if (service == null || service.engine() == null) return null;
        final DexEngine engine = service.engine();
        final DexBridge bridge = service.bridge();
        return engine.call(() -> {
            final Waiting w = new Waiting();
            w.pairing = bridge.pairing();
            w.keysWanted = bridge.keysWanted();
            w.keysExist = engine.keysExist();
            w.keysReady = engine.keysReady();
            w.keysBusy = engine.keysBusy();
            w.keysMessage = engine.keysMessage();
            w.approval = engine.awaitingApproval();
            w.snapshot = engine.tradeSnapshot();
            w.commitment = engine.tradeCommitment();
            w.startRefusal = engine.startRefusal();
            w.returnWanted = bridge.returnWanted();
            w.unlocked = engine.tradingUnlocked();
            final JSONObject wallet = engine.walletStatus();
            w.walletBusy = wallet.optBoolean("busy");
            w.walletError = wallet.optString("error");
            final JSONObject offer = w.snapshot == null ? null : w.snapshot.optJSONObject("offer");
            if (offer != null) w.payoutAccount = bridge.payoutAccount(offer.optLong("chainId"));
            return w;
        }, null);
    }

    private void show(String kind, String titleText, String bodyText) {
        if (!kind.equals(showing)) {
            password.setText("");
            repeat.setText("");
            if (!kind.equals("done")) notice = "";
        }
        showing = kind;
        title.setText(titleText);
        body.setText(bodyText);
        code.setVisibility(View.GONE);
        password.setVisibility(View.GONE);
        repeat.setVisibility(View.GONE);
        no.setVisibility(View.GONE);
        later.setVisibility(View.VISIBLE);
        yes.setVisibility(View.VISIBLE);
        yes.setEnabled(true);
        later.setText(R.string.dex_not_now);
        later.setOnClickListener(v -> finish());
        note.setText(notice);
        note.setVisibility(notice.isEmpty() ? View.GONE : View.VISIBLE);
    }

    private void refresh() {
        if (isFinishing()) return;
        if (wordsFor != null) return;   // the recovery words stay until the user closes them
        final Waiting w = waiting();
        final DexService service = DexService.get();
        if (w == null || service == null) {
            nothing();
            return;
        }
        final DexEngine engine = service.engine();
        final DexBridge bridge = service.bridge();
        if (w.pairing != null) {
            final DexBridge.Pairing p = w.pairing;
            final StringBuilder accounts = new StringBuilder();
            if (!p.account.isEmpty()) accounts.append(p.account);
            if (!p.solAccount.isEmpty()) accounts.append(accounts.length() > 0 ? "\n" : "").append(p.solAccount);
            show("pair", getString(R.string.dex_pair_title), getString(R.string.dex_pair_body, p.origin, accounts));
            code.setText(p.code);
            code.setVisibility(View.VISIBLE);
            yes.setText(R.string.dex_accept);
            yes.setOnClickListener(v -> engine.post(() -> bridge.acceptPairing(p.id)));
            later.setText(R.string.dex_refuse);
            later.setOnClickListener(v -> {
                engine.post(() -> bridge.refusePairing(p.id));
                finish();
            });
            return;
        }
        if (w.keysWanted && !w.keysReady) {
            final boolean fresh = !w.keysExist;
            if (!w.keysMessage.isEmpty()) notice = w.keysMessage;
            // A refused password is cleared, so the next try starts from an empty field
            if (clearOnFail && !w.keysBusy && !w.keysMessage.isEmpty()) {
                clearOnFail = false;
                password.setText("");
                repeat.setText("");
            }
            show("keys", getString(fresh ? R.string.dex_keys_new_title : R.string.dex_keys_unlock_title),
                    getString(w.keysBusy ? R.string.dex_keys_working : fresh ? R.string.dex_keys_new_body : R.string.dex_keys_unlock_body));
            password.setHint(fresh ? R.string.dex_password_new : R.string.dex_password);
            password.setVisibility(View.VISIBLE);
            repeat.setVisibility(fresh ? View.VISIBLE : View.GONE);
            password.setEnabled(!w.keysBusy);
            repeat.setEnabled(!w.keysBusy);
            yes.setEnabled(!w.keysBusy);
            yes.setText(fresh ? R.string.dex_keys_create : R.string.dex_keys_unlock);
            yes.setOnClickListener(v -> {
                final String typed = password.getText().toString();
                if (fresh && typed.length() < 8) notice = getString(R.string.dex_password_short);
                else if (fresh && !typed.equals(repeat.getText().toString())) notice = getString(R.string.dex_password_mismatch);
                else {
                    notice = "";
                    askedFresh = fresh;
                    clearOnFail = true;
                    if (fresh) wordsPassword = typed;
                    engine.post(() -> engine.unlockKeys(typed));
                }
                note.setText(notice);
                note.setVisibility(notice.isEmpty() ? View.GONE : View.VISIBLE);
            });
            later.setOnClickListener(v -> {
                engine.post(bridge::dismissKeys);
                finish();
            });
            return;
        }
        if (askedFresh && w.keysReady && wordsPassword != null) {
            // Keys just made: show their recovery words once, before anything else
            askedFresh = false;
            showWords(engine, wordsPassword);
            wordsPassword = null;
            return;
        }
        if (w.approval && w.snapshot != null && w.snapshot.optJSONObject("offer") != null) {
            final JSONObject offer = w.snapshot.optJSONObject("offer");
            final long chain = offer.optLong("chainId");
            final boolean paysCoin = w.snapshot.optString("role").equals("owner");
            final String coin = DexText.coinText(offer), xmz = DexText.xmzText(offer);
            if (!w.startRefusal.isEmpty()) notice = w.startRefusal;
            show("approve", getString(R.string.dex_approve_title), getString(R.string.dex_approve_body, paysCoin ? coin : xmz, paysCoin ? xmz : coin,
                    DexConfig.networkName(chain), DexConfig.coin(chain), w.payoutAccount.isEmpty() ? getString(R.string.dex_approve_no_account) : w.payoutAccount));
            final String commitment = w.commitment;
            yes.setText(R.string.dex_approve);
            yes.setOnClickListener(v -> engine.post(() -> engine.approveTrade(commitment)));
            no.setVisibility(View.VISIBLE);
            no.setText(R.string.dex_approve_cancel);
            no.setOnClickListener(v -> {
                engine.post(engine::abandonTrade);
                finish();
            });
            return;
        }
        if (w.returnWanted || returnAsked) {
            final String home = DexService.prefs(this).getString(DexService.PREF_HOME, "");
            final boolean any = !w.unlocked.equals("0");
            if (returnAsked && !w.walletBusy) {
                notice = getString(w.walletError.isEmpty() ? R.string.dex_return_sent : R.string.dex_return_failed);
                show("done", getString(R.string.dex_return_title), "");
                yes.setVisibility(View.GONE);
                later.setText(R.string.dex_close);
                later.setOnClickListener(v -> {
                    returnAsked = false;
                    engine.post(bridge::dismissReturn);
                    finish();
                });
                return;
            }
            show("return", getString(R.string.dex_return_title), home.isEmpty() ? getString(R.string.dex_status_home_none)
                    : any ? getString(R.string.dex_return_body, w.unlocked, home) : getString(R.string.dex_return_none));
            yes.setText(R.string.dex_return_go);
            yes.setEnabled(any && !home.isEmpty() && !returnAsked);
            yes.setOnClickListener(v -> {
                returnAsked = true;
                yes.setEnabled(false);
                engine.post(() -> {
                    if (!engine.returnXmz(home)) runOnUiThread(() -> {
                        returnAsked = false;
                        notice = getString(R.string.dex_return_failed);
                        refresh();
                    });
                });
            });
            later.setOnClickListener(v -> {
                engine.post(bridge::dismissReturn);
                finish();
            });
            return;
        }
        nothing();
    }

    private void showWords(final DexEngine engine, final String typed) {
        wordsFor = typed;
        show("words", getString(R.string.dex_words_title), getString(R.string.dex_keys_working));
        yes.setVisibility(View.GONE);
        later.setVisibility(View.GONE);
        new Thread(() -> {
            final String words = engine.recoveryWords(typed);
            runOnUiThread(() -> {
                body.setText(words == null ? getString(R.string.dex_words_wrong) : getString(R.string.dex_words_body, numbered(words)));
                later.setVisibility(View.VISIBLE);
                later.setText(words == null ? R.string.dex_close : R.string.dex_words_done);
                later.setOnClickListener(v -> {
                    wordsFor = null;
                    body.setText("");
                    refresh();
                });
            });
        }, "dex-words").start();
    }

    static String numbered(String words) {
        final String[] parts = words.split(" ");
        final StringBuilder out = new StringBuilder();
        for (int i = 0; i < parts.length; i++) out.append(i + 1).append(". ").append(parts[i]).append(i % 3 == 2 ? "\n" : "    ");
        return out.toString().trim();
    }

    private void nothing() {
        // A request was just answered and nothing else is waiting: back to where the user was (the browser)
        if (!showing.isEmpty() && !showing.equals("nothing")) {
            finish();
            return;
        }
        show("nothing", getString(R.string.dex_title), getString(R.string.dex_prompt_nothing));
        yes.setVisibility(View.GONE);
        later.setText(R.string.dex_close);
        later.setOnClickListener(v -> finish());
    }
}
