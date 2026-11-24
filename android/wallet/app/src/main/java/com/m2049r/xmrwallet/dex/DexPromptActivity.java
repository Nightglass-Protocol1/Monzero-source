package com.m2049r.xmrwallet.dex;

import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
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
    private EditText password, repeat, address, amount;
    /** The Monero address last sent to (the user's Cake Wallet, say), offered again next time */
    static final String PREF_XMR_HOME = "dex_xmr_home";
    private boolean xmrOutAsked;
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
        boolean xmrAdd, xmrOut, xmrReady, xmrBusy;
        String xmrAddAmount = "", xmrAddress = "", xmrUnlocked = "0", xmrError = "";
        String keysMessage = "", commitment = "", startRefusal = "", unlocked = "0", payoutAccount = "", walletError = "";
        JSONObject snapshot;
        // A direct (coin <-> coin) take of ours waiting for approval, and where its coin goes
        JSONObject direct;
        String directMessage = "", directPayout = "";
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
        address = findViewById(R.id.dexPromptAddress);
        amount = findViewById(R.id.dexPromptAmount);
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

    /**
     * Leaving the "move it back" screen any way (Back, Home, another app) withdraws the question,
     * like "Not now". Otherwise it stayed asked and the wallet reopened this screen every time it
     * came to the front. A send already started carries on.
     */
    @Override
    protected void onStop() {
        if (!isChangingConfigurations() && (showing.equals("xmr-add") || showing.equals("xmr-out") || showing.equals("xmr-done"))) {
            xmrOutAsked = false;
            showing = "";
            final DexService service = DexService.get();
            if (service != null && service.engine() != null && service.bridge() != null) service.engine().post(service.bridge()::dismissXmr);
        }
        if (!isChangingConfigurations() && (showing.equals("return") || showing.equals("done"))) {
            returnAsked = false;
            showing = "";
            final DexService service = DexService.get();
            if (service != null && service.engine() != null && service.bridge() != null) service.engine().post(service.bridge()::dismissReturn);
        }
        super.onStop();
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
            w.xmrAdd = bridge.xmrAddWanted();
            w.xmrOut = bridge.xmrOutWanted();
            w.xmrAddAmount = bridge.xmrAddAmount();
            final JSONObject xw = engine.xmrWalletStatus();
            w.xmrReady = "ready".equals(xw.optString("state"));
            w.xmrBusy = xw.optBoolean("busy");
            w.xmrAddress = xw.optString("address");
            w.xmrUnlocked = xw.optString("unlocked", "0");
            w.xmrError = !xw.optString("message").isEmpty() ? xw.optString("message") : xw.optString("error");
            w.unlocked = engine.tradingUnlocked();
            final JSONObject wallet = engine.walletStatus();
            w.walletBusy = wallet.optBoolean("busy");
            w.walletError = wallet.optString("error");
            final JSONObject offer = w.snapshot == null ? null : w.snapshot.optJSONObject("offer");
            if (offer != null) w.payoutAccount = bridge.payoutAccount(offer.optLong("chainId"));
            w.direct = engine.directAwaitingApproval();
            w.directMessage = engine.directMessage();
            final JSONObject gets = w.direct == null ? null : w.direct.optJSONObject("trade").optJSONObject("gets");
            if (gets != null) w.directPayout = bridge.payoutAccount(gets.optLong("chainId"));
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
        address.setVisibility(View.GONE);
        amount.setVisibility(View.GONE);
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
            final long chain = DexConfig.marketOf(offer.optLong("chainId"), offer.optString("asset"));
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
        if (w.direct != null && w.direct.optJSONObject("trade") != null) {
            // The terms are the wallet's own (the trade's journal), never the page's
            final JSONObject t = w.direct.optJSONObject("trade"), gives = t.optJSONObject("gives"), gets = t.optJSONObject("gets");
            if (gives != null && gets != null) {
                // Coins by market id (USDT has its own decimals), networks by chain
                final long gc = DexConfig.marketOf(gives.optLong("chainId"), gives.optString("asset")), wc = DexConfig.marketOf(gets.optLong("chainId"), gets.optString("asset"));
                final String pays = DexFiles.units(gives.optString("value"), DexDirect.decimals(gc)) + " " + DexDirect.ticker(gc);
                final String receives = DexFiles.units(gets.optString("value"), DexDirect.decimals(wc)) + " " + DexDirect.ticker(wc);
                if (!w.directMessage.isEmpty()) notice = w.directMessage;
                show("approve-direct", getString(R.string.dex_approve_title), getString(R.string.dex_direct_approve_body, pays, receives,
                        DexConfig.networkName(DexConfig.helperChain(gc)), DexConfig.networkName(DexConfig.helperChain(wc)), w.directPayout.isEmpty() ? getString(R.string.dex_approve_no_account) : w.directPayout));
                final String folder = w.direct.optString("folder"), commitment = t.optString("commitment");
                yes.setText(R.string.dex_approve);
                yes.setOnClickListener(v -> engine.post(() -> engine.approveDirect(folder, commitment)));
                return;
            }
        }
        if (w.xmrAdd) {
            showXmrAdd(engine, bridge, w);
            return;
        }
        if (w.xmrOut || xmrOutAsked) {
            showXmrOut(engine, bridge, w);
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

    /**
     * Add XMR: the Monero trading wallet's address (and the amount the page named), with a button
     * that opens Cake Wallet (or any Monero wallet the phone has) with both filled in, through a
     * standard monero: payment link. Nothing is sent from here: the user confirms in their wallet.
     */
    private void showXmrAdd(final DexEngine engine, final DexBridge bridge, Waiting w) {
        final String xmr = w.xmrAddAmount;
        if (!w.xmrReady || w.xmrAddress.isEmpty()) {
            show("xmr-add", getString(R.string.dex_xmr_add_title), getString(R.string.dex_xmr_not_ready));
            yes.setVisibility(View.GONE);
        } else {
            show("xmr-add", getString(R.string.dex_xmr_add_title), getString(R.string.dex_xmr_add_body,
                    xmr.isEmpty() ? getString(R.string.dex_xmr_add_any) : xmr + " XMR", w.xmrAddress));
            final String to = w.xmrAddress;
            yes.setText(R.string.dex_xmr_add_open);
            yes.setOnClickListener(v -> {
                final Intent pay = new Intent(Intent.ACTION_VIEW, Uri.parse("monero:" + to + (xmr.isEmpty() ? "" : "?tx_amount=" + xmr)));
                try {
                    startActivity(pay);
                } catch (ActivityNotFoundException e) {
                    notice = getString(R.string.dex_xmr_add_no_app);
                    note.setText(notice);
                    note.setVisibility(View.VISIBLE);
                }
            });
            no.setVisibility(View.VISIBLE);
            no.setText(R.string.dex_xmr_add_copy);
            no.setOnClickListener(v -> {
                final ClipboardManager clip = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                if (clip != null) clip.setPrimaryClip(ClipData.newPlainText("Monero address", to));
                notice = getString(R.string.dex_xmr_add_copied);
                note.setText(notice);
                note.setVisibility(View.VISIBLE);
            });
        }
        later.setText(R.string.dex_close);
        later.setOnClickListener(v -> {
            engine.post(bridge::dismissXmr);
            finish();
        });
    }

    /**
     * Send XMR out of the Monero trading wallet to the user's own Monero wallet. The address is
     * typed or pasted here, in the wallet (the page never names it), and remembered for next time.
     */
    private void showXmrOut(final DexEngine engine, final DexBridge bridge, Waiting w) {
        if (xmrOutAsked && !w.xmrBusy) {
            notice = w.xmrError.isEmpty() ? getString(R.string.dex_return_sent) : w.xmrError;
            show("xmr-done", getString(R.string.dex_xmr_out_title), "");
            yes.setVisibility(View.GONE);
            later.setText(R.string.dex_close);
            later.setOnClickListener(v -> {
                xmrOutAsked = false;
                engine.post(bridge::dismissXmr);
                finish();
            });
            return;
        }
        if (!w.xmrError.isEmpty() && !xmrOutAsked && showing.equals("xmr-out")) notice = w.xmrError;
        final boolean fresh = !showing.equals("xmr-out");
        final boolean any = w.xmrReady && !w.xmrUnlocked.equals("0");
        show("xmr-out", getString(R.string.dex_xmr_out_title), any ? getString(R.string.dex_xmr_out_body, w.xmrUnlocked) : getString(R.string.dex_xmr_out_none));
        address.setVisibility(View.VISIBLE);
        amount.setVisibility(View.VISIBLE);
        if (fresh) {
            address.setText(DexService.prefs(this).getString(PREF_XMR_HOME, ""));
            amount.setText("");
        }
        yes.setText(R.string.dex_xmr_out_go);
        yes.setEnabled(any && !xmrOutAsked);
        yes.setOnClickListener(v -> {
            final String to = address.getText().toString().trim(), xmr = amount.getText().toString().trim();
            // Main network: 4 (address) or 8 (subaddress); stagenet (test build): 5 or 7
            if (!to.matches(DexConfig.mainnet() ? "\\A[48][0-9A-Za-z]{94}\\z" : "\\A[57][0-9A-Za-z]{94}\\z")) {
                notice = getString(R.string.dex_xmr_out_bad);
                note.setText(notice);
                note.setVisibility(View.VISIBLE);
                return;
            }
            DexService.prefs(this).edit().putString(PREF_XMR_HOME, to).apply();
            xmrOutAsked = true;
            yes.setEnabled(false);
            engine.post(() -> {
                if (!engine.sendXmr(to, xmr)) runOnUiThread(() -> {
                    xmrOutAsked = false;
                    refresh();
                });
            });
        });
        later.setOnClickListener(v -> {
            xmrOutAsked = false;
            engine.post(bridge::dismissXmr);
            finish();
        });
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
