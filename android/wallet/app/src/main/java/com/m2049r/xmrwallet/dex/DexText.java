package com.m2049r.xmrwallet.dex;

import android.content.Context;

import com.m2049r.xmrwallet.R;

import org.json.JSONObject;

import java.math.BigInteger;

/** Words for what a trade is doing, and its amounts as the user reads them. */
final class DexText {
    private DexText() {
    }

    static String stage(Context c, JSONObject snapshot) {
        final String stage = snapshot.optString("stage");
        final int id;
        switch (stage) {
            case "Start":
                id = R.string.dex_stage_start;
                break;
            case "LockSent":
                id = R.string.dex_stage_lock_sent;
                break;
            case "Locked":
                id = R.string.dex_stage_locked;
                break;
            case "ReadySent":
                id = R.string.dex_stage_ready_sent;
                break;
            case "Ready":
                id = R.string.dex_stage_ready;
                break;
            case "Sweeping":
                id = R.string.dex_stage_sweeping;
                break;
            case "WaitingForEvmLock":
                id = R.string.dex_stage_waiting_lock;
                break;
            case "XmzLockSent":
                id = R.string.dex_stage_xmz_lock_sent;
                break;
            case "XmzLocked":
                id = R.string.dex_stage_xmz_locked;
                break;
            case "ClaimSent":
                id = R.string.dex_stage_claim_sent;
                break;
            case "RefundSent":
                id = R.string.dex_stage_refund_sent;
                break;
            case "WaitingForRefund":
                id = R.string.dex_stage_waiting_refund;
                break;
            case "SweepingBack":
                id = R.string.dex_stage_sweeping_back;
                break;
            default:
                if (stage.equals("Done(Swapped)")) return c.getString(R.string.dex_stage_done_swapped);
                if (stage.startsWith("Done(")) return c.getString(R.string.dex_stage_done_other, stage.substring(5, stage.length() - 1).toLowerCase(java.util.Locale.ROOT));
                return stage;
        }
        String text = c.getString(id);
        final JSONObject progress = snapshot.optJSONObject("progress");
        if (progress != null) {
            final JSONObject count = progress.optJSONObject(stage.startsWith("Sweeping") ? "xmzSweep" : "xmzLock");
            if (count != null && count.optLong("needed") > 0) text += " (" + count.optLong("confirmations") + "/" + count.optLong("needed") + ")";
        }
        return text;
    }

    /** The coin side of an offer, as "0.004 BNB". */
    static String coinText(JSONObject offer) {
        final long chain = offer.optLong("chainId");
        try {
            final String v = offer.optString("value");
            final BigInteger value = v.startsWith("0x") ? new BigInteger(v.substring(2), 16) : new BigInteger(v);
            return DexFiles.units(value.toString(), DexConfig.decimals(chain)) + " " + DexConfig.coin(chain);
        } catch (Exception e) {
            return "? " + DexConfig.coin(chain);
        }
    }

    /** The XMZ side of an offer, as "2000 XMZ". */
    static String xmzText(JSONObject offer) {
        final String atoms = DexMarket.amountText(offer.opt("xmzAmount"));
        return (atoms == null ? "?" : DexFiles.units(atoms, 11)) + " XMZ";
    }
}
