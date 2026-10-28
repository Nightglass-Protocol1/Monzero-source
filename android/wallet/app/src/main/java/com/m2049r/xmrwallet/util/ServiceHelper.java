package com.m2049r.xmrwallet.util;

import com.m2049r.xmrwallet.model.NetworkType;
import com.m2049r.xmrwallet.model.WalletManager;
import androidx.annotation.NonNull;

import com.m2049r.xmrwallet.service.exchange.api.ExchangeApi;
import com.m2049r.xmrwallet.service.exchange.api.ExchangeCallback;
import com.m2049r.xmrwallet.service.exchange.api.ExchangeException;
import com.m2049r.xmrwallet.service.exchange.api.ExchangeRate;

import okhttp3.HttpUrl;

public class ServiceHelper {

    // No market lists XMZ yet; the Monero price feed must not be shown as an XMZ price.
    static public ExchangeApi getExchangeApi() {
        return new ExchangeApi() {
            @Override
            public void queryExchangeRate(@NonNull String baseCurrency, @NonNull String quoteCurrency,
                                          @NonNull ExchangeCallback callback) {
                if (baseCurrency.equals(quoteCurrency)) {
                    callback.onSuccess(new ExchangeRate() {
                        @Override public String getServiceName() { return "none"; }
                        @Override public String getBaseCurrency() { return baseCurrency; }
                        @Override public String getQuoteCurrency() { return quoteCurrency; }
                        @Override public double getRate() { return 1.0; }
                    });
                } else {
                    callback.onError(new ExchangeException("No " + baseCurrency + " market price"));
                }
            }

            @Override
            public String getName() {
                return "none";
            }
        };
    }

    static private final ExchangeApi[] fiatApis = new ExchangeApi[]{
            new com.m2049r.xmrwallet.service.exchange.ecb.ExchangeApiImpl(),
            new com.m2049r.xmrwallet.service.exchange.yadio.ExchangeApiImpl()};

    static public ExchangeApi getFiatApi(String symbol) {
        return (symbol.length() == 3) ? fiatApis[0] : fiatApis[1];
    }
}
