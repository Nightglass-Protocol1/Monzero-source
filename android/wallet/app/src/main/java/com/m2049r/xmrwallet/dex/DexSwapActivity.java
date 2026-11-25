package com.m2049r.xmrwallet.dex;

import android.Manifest;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.widget.SwitchCompat;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.google.zxing.BarcodeFormat;
import com.google.zxing.EncodeHintType;
import com.google.zxing.common.BitMatrix;
import com.google.zxing.qrcode.QRCodeWriter;
import com.m2049r.xmrwallet.R;
import com.m2049r.xmrwallet.SecureActivity;

import org.json.JSONArray;
import org.json.JSONObject;

import java.math.BigDecimal;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The Swap screen in the wallet: buy (or sell) an amount of XMZ with a coin, one button. It takes
 * the best offers by itself (as many as the amount needs), waits for a deposit when the trade
 * account is short (one address, a QR code, one amount with the network fees) and starts once it
 * arrives; a sell short of XMZ opens the wallet's Send screen to the trading wallet and starts once
 * the XMZ unlocks. Every trade is a row with a tick per step. The same rules as the desktop
 * wallet's Swap screen; the engine (DexService) does the trading.
 */
public class DexSwapActivity extends SecureActivity {
    private static final String[] COINS = {"SOL", "ETH", "BNB", "USDT", "USDT-BSC"};

    private String side = "buy", coin = "SOL", stage = "form", note = "";
    private BigInteger quoted;                // the coin agreed to (to notice a price change while waiting)
    private int started;
    private final Map<String, BigInteger> balances = new HashMap<>();
    private long balancesAt;
    private boolean reading;
    private JSONObject markets = new JSONObject(), wallet = new JSONObject();
    private JSONArray trades = new JSONArray();
    private boolean autoApprove = true;

    private TextView ready, quote, fees, waitText, waitNote, address, noteView, tradesTitle;
    private Button readyButton, swap, takeAvailable, waitButton, cancel, approveAll;
    private EditText amount;
    private RadioGroup coins;
    private LinearLayout form, waitBox, tradeList;
    private ImageView qr;
    private SwitchCompat auto;
    private final Runnable refresh = this::refresh;
    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            onTick();
            if (!isFinishing()) tradeList.postDelayed(this, 15000);
        }
    };

    public static void open(Context context, String homeAddress, String daemon) {
        context.startActivity(new Intent(context, DexSwapActivity.class).putExtra(DexActivity.EXTRA_HOME, homeAddress).putExtra(DexActivity.EXTRA_DAEMON, daemon));
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_dex_swap);
        // As the DEX screen: the open wallet is where trading XMZ goes back to, its node the trading wallet's
        final String home = getIntent().getStringExtra(DexActivity.EXTRA_HOME), daemon = getIntent().getStringExtra(DexActivity.EXTRA_DAEMON);
        if (home != null && home.matches("[0-9A-Za-z]{90,110}")) DexService.prefs(this).edit().putString(DexService.PREF_HOME, home).apply();
        if (DexConfig.daemonAddress(daemon)) DexService.prefs(this).edit().putString(DexService.PREF_DAEMON, daemon).apply();

        ready = findViewById(R.id.swapReady);
        readyButton = findViewById(R.id.swapReadyButton);
        form = findViewById(R.id.swapForm);
        coins = findViewById(R.id.swapCoins);
        amount = findViewById(R.id.swapAmount);
        quote = findViewById(R.id.swapQuote);
        fees = findViewById(R.id.swapFees);
        takeAvailable = findViewById(R.id.swapTakeAvailable);
        swap = findViewById(R.id.swapButton);
        waitBox = findViewById(R.id.swapWait);
        waitText = findViewById(R.id.swapWaitText);
        qr = findViewById(R.id.swapQr);
        address = findViewById(R.id.swapAddress);
        waitButton = findViewById(R.id.swapWaitButton);
        waitNote = findViewById(R.id.swapWaitNote);
        cancel = findViewById(R.id.swapCancel);
        noteView = findViewById(R.id.swapNote);
        tradesTitle = findViewById(R.id.swapTradesTitle);
        tradeList = findViewById(R.id.swapTrades);
        auto = findViewById(R.id.swapAutoApprove);
        approveAll = findViewById(R.id.swapApproveAll);

        ((RadioGroup) findViewById(R.id.swapSide)).setOnCheckedChangeListener((g, id) -> {
            side = id == R.id.swapSell ? "sell" : "buy";
            ((TextView) findViewById(R.id.swapCoinLabel)).setText(side.equals("buy") ? R.string.dex_swap_pay_with : R.string.dex_swap_paid_in);
            note = "";
            show();
        });
        for (String c : COINS) {
            final RadioButton b = new RadioButton(this);
            b.setText(label(c));
            b.setTag(c);
            b.setId(View.generateViewId());
            b.setPadding(4, 0, 24, 0);
            coins.addView(b);
            if (c.equals(coin)) b.setChecked(true);
        }
        coins.setOnCheckedChangeListener((g, id) -> {
            final View b = g.findViewById(id);
            if (b == null) return;
            coin = (String) b.getTag();
            note = "";
            refreshOffers();
            readBalances(true);
            show();
        });
        amount.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int a, int b, int c) {
            }

            @Override
            public void onTextChanged(CharSequence s, int a, int b, int c) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                note = "";
                show();
            }
        });
        swap.setOnClickListener(v -> swap());
        takeAvailable.setOnClickListener(v -> {
            final DexSwapPlan.Plan p = plan();
            if (p != null && p.atoms > 0) amount.setText(DexFiles.units(String.valueOf(p.atoms), 11));
        });
        cancel.setOnClickListener(v -> {
            stage = "form";
            note = "";
            show();
        });
        readyButton.setOnClickListener(v -> fixReady());
        auto.setOnClickListener(v -> {
            final DexService s = DexService.get();
            final boolean on = auto.isChecked();
            if (s != null) s.engine().post(() -> s.engine().setAutoApprove(on));
        });
        approveAll.setOnClickListener(v -> {
            final DexService s = DexService.get();
            if (s != null) s.engine().post(() -> s.engine().approveAll());
        });
        findViewById(R.id.swapMore).setOnClickListener(v -> startActivity(new Intent(this, DexActivity.class)));
    }

    @Override
    protected void onResume() {
        super.onResume();
        DexService.addUiListener(refresh);
        refresh();
        refreshOffers();
        tradeList.post(tick);
    }

    @Override
    protected void onPause() {
        DexService.removeUiListener(refresh);
        tradeList.removeCallbacks(tick);
        super.onPause();
    }

    // ---- what the engine says ------------------------------------------------------------------

    private DexEngine engine() {
        final DexService s = DexService.get();
        return s == null ? null : s.engine();
    }

    private void refresh() {
        if (isFinishing()) return;
        final DexEngine e = engine();
        if (e == null) {
            wallet = new JSONObject();
            markets = new JSONObject();
            trades = new JSONArray();
            show();
            return;
        }
        final Object[] got = e.call(() -> new Object[]{e.walletStatus(), e.marketStatus(), e.trades(), e.autoApprove(), e.marketMessage(),
                e.evmAccount(), e.solAccount(), e.solUsdtAccount()}, null);
        if (got == null) return;
        wallet = (JSONObject) got[0];
        markets = (JSONObject) got[1];
        trades = (JSONArray) got[2];
        autoApprove = (Boolean) got[3];
        try {
            wallet.put("message", got[4]).put("evm", got[5]).put("sol", got[6]).put("solUsdt", got[7]);
        } catch (Exception ignored) {
        }
        show();
    }

    private void refreshOffers() {
        final DexEngine e = engine();
        if (e == null) return;
        final String pair = coin + "/XMZ";
        e.post(() -> e.refreshOffers(pair));
    }

    /** Every 15 s: offers, balances, and a waiting swap that can start now */
    private void onTick() {
        refreshOffers();
        readBalances(stage.equals("deposit"));
        refresh();
        if ((stage.equals("deposit") || stage.equals("moving")) && funded()) start();
    }

    private void readBalances(boolean now) {
        final DexEngine e = engine();
        if (e == null || reading || (!now && System.currentTimeMillis() - balancesAt < 60000)) return;
        final long market = marketId(coin), gasMarket = marketId(DexSwapPlan.feeCoin(coin));
        final String rpc = e.rpcFor(market), gasRpc = e.rpcFor(gasMarket);
        final String evm = wallet.optString("evm"), sol = wallet.optString("sol"), solUsdt = wallet.optString("solUsdt");
        final String c = coin, gas = DexSwapPlan.feeCoin(coin);
        reading = true;
        new Thread(() -> {
            final BigInteger have = DexBalances.read(c, rpc, evm, sol, solUsdt);
            final BigInteger haveGas = gas.equals(c) ? have : DexBalances.read(gas, gasRpc, evm, sol, solUsdt);
            runOnUiThread(() -> {
                reading = false;
                if (have != null) balances.put(c, have);
                if (haveGas != null) balances.put(gas, haveGas);
                balancesAt = System.currentTimeMillis();
                if ((stage.equals("deposit") || stage.equals("moving")) && funded()) start();
                else show();
            });
        }, "dex-balance").start();
    }

    // ---- the plan ------------------------------------------------------------------------------

    private long atoms() {
        final String t = amount.getText().toString().replace(",", "").trim();
        if (!t.matches("[0-9]+(\\.[0-9]{0,11})?")) return 0;
        try {
            return new BigDecimal(t).movePointRight(11).longValueExact();
        } catch (Exception e) {
            return 0;
        }
    }

    private DexSwapPlan.Plan plan() {
        final JSONObject m = markets.optJSONObject(coin);
        if (m == null || !m.optBoolean("configured")) return null;
        final String me = coin.equals("SOL") || coin.equals("USDT") ? wallet.optString("sol") : wallet.optString("evm");
        final List<DexSwapPlan.Row> rows = DexSwapPlan.book(m.optJSONArray("listings"), coin, side, me, System.currentTimeMillis() / 1000, decimals(coin));
        return DexSwapPlan.plan(rows, atoms());
    }

    private boolean token() {
        return coin.equals("USDT") || coin.equals("USDT-BSC");
    }

    private BigInteger reserveUnits(int legs) {
        final String gas = DexSwapPlan.feeCoin(coin);
        return new BigDecimal(DexSwapPlan.feeReserve(coin)).multiply(BigDecimal.valueOf(legs)).movePointRight(decimals(gas)).toBigInteger();
    }

    /** What a buy still needs in the trade account: {coin short, fee coin short} (smallest units), null while unknown */
    private BigInteger[] shortfall(DexSwapPlan.Plan p) {
        final BigInteger have = balances.get(coin);
        final String gas = DexSwapPlan.feeCoin(coin);
        final BigInteger haveGas = balances.get(gas);
        if (have == null || (token() && haveGas == null)) return null;
        final BigInteger needCoin = token() ? p.coin : p.coin.add(reserveUnits(p.legs.size()));
        final BigInteger needGas = token() ? reserveUnits(p.legs.size()) : BigInteger.ZERO;
        return new BigInteger[]{needCoin.subtract(have).max(BigInteger.ZERO), token() ? needGas.subtract(haveGas).max(BigInteger.ZERO) : BigInteger.ZERO};
    }

    private long xmzUnlocked() {
        try {
            return new BigDecimal(wallet.optString("unlocked", "0")).movePointRight(11).longValue();
        } catch (Exception e) {
            return -1;
        }
    }

    private boolean funded() {
        final DexSwapPlan.Plan p = plan();
        if (p == null || p.legs.isEmpty()) return false;
        if (side.equals("sell")) return xmzUnlocked() >= DexSwapPlan.sellNeed(p.atoms, p.legs.size());
        final BigInteger[] s = shortfall(p);
        return s != null && s[0].signum() == 0 && s[1].signum() == 0;
    }

    // ---- actions -------------------------------------------------------------------------------

    private void swap() {
        note = "";
        final DexSwapPlan.Plan p = plan();
        if (p == null || p.legs.isEmpty()) {
            note = getString(R.string.dex_swap_no_offers);
            show();
            return;
        }
        quoted = p.coin;
        if (funded()) {
            start();
            return;
        }
        stage = side.equals("buy") ? "deposit" : "moving";
        readBalances(true);
        show();
    }

    /** Take the planned offers now (planned again from the current offers first) */
    private void start() {
        final DexEngine e = engine();
        final DexSwapPlan.Plan p = plan();
        if (e == null || p == null || p.legs.isEmpty()) {
            note = getString(R.string.dex_swap_no_offers);
            stage = "form";
            show();
            return;
        }
        // The price moved against the user by more than 3% while waiting: ask again
        if (quoted != null && quoted.signum() > 0) {
            final BigInteger limit = quoted.multiply(BigInteger.valueOf(side.equals("buy") ? 103 : 97)).divide(BigInteger.valueOf(100));
            if (side.equals("buy") ? p.coin.compareTo(limit) > 0 : p.coin.compareTo(limit) < 0) {
                note = getString(R.string.dex_swap_price_moved);
                stage = "form";
                show();
                return;
            }
        }
        final List<String[]> takes = new ArrayList<>();
        for (DexSwapPlan.Leg l : p.legs) takes.add(new String[]{l.row.id, l.takeText()});
        final Boolean ok = e.call(() -> e.takeOffers(takes), false);
        if (ok == null || !ok) {
            note = getString(R.string.dex_swap_failed);
            stage = "form";
        } else {
            started = takes.size();
            stage = "started";
            quoted = null;
        }
        show();
    }

    private void fixReady() {
        final DexService s = DexService.get();
        if (s == null) {
            if (Build.VERSION.SDK_INT >= 33 && ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
                ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.POST_NOTIFICATIONS}, 1);
            DexService.start(this);
            readyButton.postDelayed(refresh, 1500);
            return;
        }
        // The keys: the prompt screen asks for the password (and shows the recovery words of new keys)
        s.engine().post(() -> s.bridge().askKeys());
        startActivity(new Intent(this, DexPromptActivity.class));
    }

    private void moveXmz(long atomsShort) {
        final DexService s = DexService.get();
        if (s == null) return;
        final String xmz = DexFiles.units(String.valueOf(((atomsShort + DexSwapPlan.XMZ - 1) / DexSwapPlan.XMZ) * DexSwapPlan.XMZ), 11);
        final String to = wallet.optString("address");
        if (!to.isEmpty()) startActivity(DexService.sendIntent(this, to, xmz));
    }

    // ---- the screen ----------------------------------------------------------------------------

    private void show() {
        if (isFinishing()) return;
        final boolean on = DexService.get() != null;
        final boolean keysReady = wallet.optBoolean("keysReady"), keysExist = wallet.optBoolean("keysExist");
        final boolean walletReady = "ready".equals(wallet.optString("state"));
        // What is not ready
        String readyText = "";
        int readyAction = 0;
        if (!on) {
            readyText = getString(R.string.dex_swap_ready_off);
            readyAction = R.string.dex_swap_ready_on;
        } else if (!keysReady) {
            readyText = getString(keysExist ? R.string.dex_swap_ready_keys : R.string.dex_swap_ready_keys_new);
            readyAction = keysExist ? R.string.dex_swap_ready_keys_button : R.string.dex_swap_ready_keys_new_button;
        } else if (!walletReady) readyText = getString(R.string.dex_swap_ready_wallet);
        ready.setText(readyText);
        ready.setVisibility(readyText.isEmpty() ? View.GONE : View.VISIBLE);
        readyButton.setVisibility(readyAction == 0 ? View.GONE : View.VISIBLE);
        if (readyAction != 0) readyButton.setText(readyAction);

        form.setVisibility(stage.equals("form") ? View.VISIBLE : View.GONE);
        waitBox.setVisibility(stage.equals("form") ? View.GONE : View.VISIBLE);
        final DexSwapPlan.Plan p = plan();
        final int d = decimals(coin);
        if (stage.equals("form")) {
            final long a = atoms();
            quote.setVisibility(a > 0 ? View.VISIBLE : View.GONE);
            fees.setVisibility(View.GONE);
            takeAvailable.setVisibility(View.GONE);
            if (a > 0) {
                if (p == null || p.legs.isEmpty()) quote.setText(getString(R.string.dex_swap_none, label(coin)));
                else {
                    quote.setText(getString(side.equals("buy") ? R.string.dex_swap_quote_buy : R.string.dex_swap_quote_sell,
                            plain(p.coin, d), label(coin), DexFiles.units(String.valueOf(p.atoms), 11)));
                    final StringBuilder f = new StringBuilder();
                    if (p.legs.size() > 1) f.append(getString(R.string.dex_swap_offers, p.legs.size())).append(' ');
                    if (side.equals("buy"))
                        f.append(getString(R.string.dex_swap_fees_buy, plain(reserveUnits(p.legs.size()), decimals(DexSwapPlan.feeCoin(coin))), DexSwapPlan.feeCoin(coin)));
                    else
                        f.append(getString(R.string.dex_swap_fees_sell, DexFiles.units(String.valueOf(DexSwapPlan.sellNeed(p.atoms, p.legs.size()) - p.atoms), 11)));
                    fees.setText(f.toString());
                    fees.setVisibility(View.VISIBLE);
                    if (p.shortOf && p.atoms > 0) {
                        takeAvailable.setText(getString(R.string.dex_swap_only, DexFiles.units(String.valueOf(p.atoms), 11)));
                        takeAvailable.setVisibility(View.VISIBLE);
                    }
                }
            }
            swap.setEnabled(on && keysReady && walletReady && p != null && !p.legs.isEmpty() && !p.shortOf);
        } else if (stage.equals("deposit")) {
            final BigInteger[] s = p == null ? null : shortfall(p);
            final String gas = DexSwapPlan.feeCoin(coin);
            String text = getString(R.string.dex_swap_deposit, s == null ? "…" : plain(s[0], d), label(coin));
            if (s != null && s[1].signum() > 0) text += " " + getString(R.string.dex_swap_deposit_gas, plain(s[1], decimals(gas)), gas);
            waitText.setText(text);
            final String account = coin.equals("SOL") || coin.equals("USDT") ? wallet.optString("sol") : wallet.optString("evm");
            address.setText(account);
            address.setVisibility(account.isEmpty() ? View.GONE : View.VISIBLE);
            setQr(account);
            waitButton.setVisibility(account.isEmpty() ? View.GONE : View.VISIBLE);
            waitButton.setText(R.string.dex_swap_copy);
            waitButton.setOnClickListener(v -> {
                final ClipboardManager cb = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                if (cb != null) cb.setPrimaryClip(ClipData.newPlainText("address", account));
                Toast.makeText(this, R.string.dex_swap_copied, Toast.LENGTH_SHORT).show();
            });
            waitNote.setText(getString(R.string.dex_swap_deposit_note, DexConfig.networkName(marketId(coin))));
            cancel.setVisibility(View.VISIBLE);
        } else if (stage.equals("moving")) {
            final long need = p == null ? 0 : Math.max(0, DexSwapPlan.sellNeed(p.atoms, p.legs.size()) - Math.max(0, xmzUnlocked()));
            waitText.setText(getString(R.string.dex_swap_move, DexFiles.units(String.valueOf(need), 11)));
            qr.setVisibility(View.GONE);
            address.setVisibility(View.GONE);
            waitButton.setVisibility(View.VISIBLE);
            waitButton.setText(getString(R.string.dex_swap_move_button, DexFiles.units(String.valueOf(((need + DexSwapPlan.XMZ - 1) / DexSwapPlan.XMZ) * DexSwapPlan.XMZ), 11)));
            waitButton.setOnClickListener(v -> moveXmz(need));
            waitNote.setText(R.string.dex_swap_move_note);
            cancel.setVisibility(View.VISIBLE);
        } else {
            waitText.setText(getString(R.string.dex_swap_started, started));
            qr.setVisibility(View.GONE);
            address.setVisibility(View.GONE);
            waitButton.setVisibility(View.VISIBLE);
            waitButton.setText(R.string.dex_swap_again);
            waitButton.setOnClickListener(v -> {
                amount.setText("");
                stage = "form";
                show();
            });
            waitNote.setText("");
            cancel.setVisibility(View.GONE);
        }
        final String message = note.isEmpty() ? wallet.optString("message") : note;
        noteView.setText(message);
        noteView.setVisibility(message.isEmpty() ? View.GONE : View.VISIBLE);
        showTrades();
    }

    private void showTrades() {
        tradeList.removeAllViews();
        tradesTitle.setVisibility(trades.length() > 0 ? View.VISIBLE : View.GONE);
        int waiting = 0;
        for (int i = 0; i < trades.length(); i++) {
            final JSONObject t = trades.optJSONObject(i);
            if (t == null) continue;
            if (t.optBoolean("awaitingApproval") && !t.optBoolean("done")) waiting++;
            tradeList.addView(tradeRow(t));
        }
        auto.setChecked(autoApprove);
        auto.setVisibility(DexService.get() != null ? View.VISIBLE : View.GONE);
        approveAll.setVisibility(!autoApprove && waiting > 0 ? View.VISIBLE : View.GONE);
        approveAll.setText(getString(R.string.dex_swap_approve_all, waiting));
    }

    private View tradeRow(JSONObject t) {
        final LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(0, 14, 0, 14);
        final boolean buyer = "owner".equals(t.optString("role"));
        final String c = label(t.optString("coin", "")), base = "XMR".equals(t.optString("privateCoin")) ? "XMR" : "XMZ";
        final int[] st = DexSwapPlan.step(t.optString("role"), t.optString("stage"));
        final boolean done = t.optBoolean("done");
        final String state = done ? getString(st[1] == 1 ? R.string.dex_swap_state_refunded : R.string.dex_swap_state_done)
                : t.optBoolean("awaitingApproval") ? getString(R.string.dex_swap_state_approval)
                : t.optBoolean("awaitingFunding") ? getString(R.string.dex_swap_state_funds) : getString(R.string.dex_swap_state_running);
        String value = t.optString("value");
        try {
            final BigInteger v = value.startsWith("0x") ? new BigInteger(value.substring(2), 16) : new BigInteger(value);
            value = plain(v, t.optInt("decimals", 18));
        } catch (Exception e) {
            value = "";
        }
        final String xmz = DexFiles.units(t.optString("xmzAmount", "0"), base.equals("XMR") ? 12 : 11);
        final TextView head = new TextView(this);
        head.setText(getString(R.string.dex_swap_row, getString(buyer ? R.string.dex_swap_buy : R.string.dex_swap_sell).split(" ")[0], xmz, value, c, state).replace(" XMZ ·", " " + base + " ·"));
        head.setTextSize(15);
        head.setTextColor(buyer ? Color.parseColor("#2FBF71") : Color.parseColor("#E5677A"));
        row.addView(head);
        final String[] steps = buyer
                ? new String[]{getString(R.string.dex_swap_step_agreed), getString(R.string.dex_swap_step_you_lock, c), getString(R.string.dex_swap_step_they_lock, base), getString(R.string.dex_swap_step_arrives, base)}
                : new String[]{getString(R.string.dex_swap_step_agreed), getString(R.string.dex_swap_step_buyer_locks, c), getString(R.string.dex_swap_step_you_lock, base), getString(R.string.dex_swap_step_arrives, c)};
        final StringBuilder line = new StringBuilder();
        for (int i = 0; i < 4; i++) {
            final boolean ok = i < st[0], now = i == st[0] && st[2] == 0;
            line.append(ok ? "✓ " : now ? (st[1] == 1 ? "↩ " : "● ") : "○ ").append(steps[i]).append(i < 3 ? "   " : "");
        }
        final TextView ticks = new TextView(this);
        ticks.setText(line.toString());
        ticks.setTextSize(13);
        row.addView(ticks);
        // A trade waiting for the user: its details and approval are on the prompt screen
        if (t.optBoolean("awaitingApproval") && !done && !autoApprove) {
            row.setOnClickListener(v -> {
                final DexService s = DexService.get();
                if (s != null) s.engine().post(() -> s.engine().selectTrade(t.optString("tradeDir")));
                startActivity(new Intent(this, DexPromptActivity.class));
            });
        }
        return row;
    }

    private void setQr(String text) {
        if (text == null || text.isEmpty()) {
            qr.setVisibility(View.GONE);
            return;
        }
        try {
            final Map<EncodeHintType, Object> hints = new HashMap<>();
            hints.put(EncodeHintType.CHARACTER_SET, "utf-8");
            hints.put(EncodeHintType.MARGIN, 1);
            final int size = 512;
            final BitMatrix m = new QRCodeWriter().encode(text, BarcodeFormat.QR_CODE, size, size, hints);
            final int[] px = new int[size * size];
            for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) px[y * size + x] = m.get(x, y) ? Color.BLACK : Color.WHITE;
            qr.setImageBitmap(Bitmap.createBitmap(px, 0, size, size, size, Bitmap.Config.RGB_565));
            qr.setVisibility(View.VISIBLE);
        } catch (Exception e) {
            qr.setVisibility(View.GONE);
        }
    }

    // ---- small helpers -------------------------------------------------------------------------

    private static String label(String c) {
        return c.equals("USDT") ? "USDT (SOL)" : c.equals("USDT-BSC") ? "USDT (BNB)" : c;
    }

    /** The market id of a coin on this network (as DexConfig names them) */
    private static long marketId(String c) {
        for (long id : DexConfig.chains()) if (DexConfig.coin(id).equals(c)) return id;
        return 0;
    }

    private static int decimals(String c) {
        final long id = marketId(c);
        return id == 0 ? 18 : DexConfig.decimals(id);
    }

    /** Smallest units as a short plain number (at most 6 decimals, rounded up so a deposit is never short) */
    private static String plain(BigInteger units, int decimals) {
        BigDecimal v = new BigDecimal(units).movePointLeft(decimals);
        v = v.setScale(Math.min(6, decimals), java.math.RoundingMode.CEILING).stripTrailingZeros();
        return v.toPlainString();
    }
}
