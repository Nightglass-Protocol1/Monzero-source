#!/usr/bin/env python3
"""Monzero's Solana RPC gateway: one address (https://sol.monzero.org) for the apps, relays and
web pages, in front of several Solana RPC providers.

- JSON-RPC over HTTP POST (single or batch) goes to the healthiest provider; on a network error,
  a timeout, HTTP 429/5xx or a provider-side error it is tried on the next one.
- WebSocket subscriptions (solana-web3.js confirms transactions with them) are piped to the
  first healthy provider that accepts the connection.
- A background check asks every provider for its slot; one that fails or lags is skipped.
- Expensive methods are refused, each client IP has a request budget, a few answers that do
  not change within a second or two are cached.
- Provider URLs (they carry API keys) live only in the config file and are never logged or shown.

Config (JSON), path in $SOL_GATEWAY_CONFIG (default /etc/sol-gateway/config.json):
  {"listen": "127.0.0.1:8899",
   "providers": [{"name": "helius", "http": "https://mainnet.helius-rpc.com/?api-key=KEY",
                  "ws": "wss://mainnet.helius-rpc.com/?api-key=KEY"}, ...],
   "rate_per_sec": 20, "burst": 100, "max_lag_slots": 150, "trust_proxy": true}
"""
import asyncio
import json
import logging
import os
import time

import aiohttp
from aiohttp import web

log = logging.getLogger("sol-gateway")

# Methods that scan large parts of the chain: costly for providers, not needed by the DEX
DENIED = {
    "getProgramAccounts", "getLargestAccounts", "getTokenLargestAccounts", "getBlocks",
    "getBlocksWithLimit", "getBlock", "getConfirmedBlock", "getConfirmedBlocks",
    "getConfirmedBlocksWithLimit", "getSupply", "getVoteAccounts", "getClusterNodes",
    "getLeaderSchedule", "getRecentPerformanceSamples", "getInflationReward",
}
# Answers that may be reused for a moment (seconds), keyed by method and params
CACHE_SECS = {
    "getSlot": 1.0, "getBlockHeight": 1.0, "getEpochInfo": 2.0, "getLatestBlockhash": 1.0,
    "getMinimumBalanceForRentExemption": 300.0, "getGenesisHash": 3600.0, "getVersion": 300.0,
    "getFeeForMessage": 1.0, "getRecentPrioritizationFees": 2.0,
}
MAX_BODY = 256 * 1024
MAX_BATCH = 20
TIMEOUT = aiohttp.ClientTimeout(total=20, connect=5)


class Provider:
    def __init__(self, p):
        self.name = p["name"]
        self.http = p["http"]
        self.ws = p.get("ws")
        self.slot = 0
        self.healthy = True
        self.failures = 0
        self.last_error = ""
        self.served = 0

    def status(self):
        return {"name": self.name, "healthy": self.healthy, "slot": self.slot, "served": self.served,
                "websocket": bool(self.ws), "lastError": self.last_error}


class Gateway:
    def __init__(self, cfg):
        self.providers = [Provider(p) for p in cfg["providers"]]
        if not self.providers:
            raise SystemExit("config: no providers")
        self.rate = float(cfg.get("rate_per_sec", 20))
        self.burst = float(cfg.get("burst", 100))
        self.max_lag = int(cfg.get("max_lag_slots", 150))
        self.trust_proxy = bool(cfg.get("trust_proxy", True))
        self.buckets = {}  # ip -> (tokens, last time)
        self.cache = {}  # key -> (expires, result)
        self.session = None

    # ---- provider choice
    def ordered(self):
        """Healthy providers first, in config order (the first is the preferred one)"""
        return [p for p in self.providers if p.healthy] + [p for p in self.providers if not p.healthy]

    async def check_loop(self):
        while True:
            await asyncio.gather(*(self.check(p) for p in self.providers))
            top = max((p.slot for p in self.providers), default=0)
            for p in self.providers:
                ok = p.failures == 0 and p.slot > 0 and top - p.slot <= self.max_lag
                if ok != p.healthy:
                    log.warning("%s is now %s (slot %s, top %s)", p.name, "healthy" if ok else "skipped", p.slot, top)
                p.healthy = ok
            await asyncio.sleep(15)

    async def check(self, p):
        try:
            async with self.session.post(p.http, json={"jsonrpc": "2.0", "id": 1, "method": "getSlot"}, timeout=aiohttp.ClientTimeout(total=8)) as r:
                body = await r.json(content_type=None)
                p.slot = int(body["result"])
                p.failures = 0
        except Exception as e:  # noqa: BLE001 (any failure marks it down)
            p.failures += 1
            p.last_error = type(e).__name__

    # ---- limits
    def client_ip(self, request):
        if self.trust_proxy and request.remote in ("127.0.0.1", "::1"):
            fwd = request.headers.get("X-Forwarded-For", "")
            if fwd:
                return fwd.split(",")[0].strip()
        return request.remote or "?"

    def allow(self, ip, cost=1.0):
        now = time.monotonic()
        tokens, last = self.buckets.get(ip, (self.burst, now))
        tokens = min(self.burst, tokens + (now - last) * self.rate)
        if tokens < cost:
            self.buckets[ip] = (tokens, now)
            return False
        self.buckets[ip] = (tokens - cost, now)
        if len(self.buckets) > 50_000:  # forget idle clients
            cutoff = now - 600
            self.buckets = {k: v for k, v in self.buckets.items() if v[1] > cutoff}
        return True

    # ---- JSON-RPC
    async def forward(self, payload):
        """Send one JSON-RPC payload (object or list) to the providers in turn"""
        last = None
        for p in self.ordered():
            try:
                async with self.session.post(p.http, json=payload, timeout=TIMEOUT) as r:
                    if r.status == 429 or r.status >= 500:
                        last = f"HTTP {r.status}"
                        p.last_error = last
                        continue
                    body = await r.json(content_type=None)
                    # A provider's own failure (not the caller's mistake): try the next one
                    if isinstance(body, dict) and body.get("error", {}).get("code") in (-32005, -32603, -32004):
                        last = f"rpc {body['error'].get('code')}"
                        p.last_error = last
                        continue
                    p.served += 1
                    return body
            except (aiohttp.ClientError, asyncio.TimeoutError, ValueError) as e:
                last = type(e).__name__
                p.last_error = last
                p.failures += 1
        raise RuntimeError(last or "no provider answered")

    async def call_one(self, req):
        if not isinstance(req, dict) or not isinstance(req.get("method"), str):
            return {"jsonrpc": "2.0", "id": None, "error": {"code": -32600, "message": "Invalid Request"}}
        method = req["method"]
        if method in DENIED:
            return {"jsonrpc": "2.0", "id": req.get("id"), "error": {"code": -32601, "message": f"{method} is not served by this gateway"}}
        ttl = CACHE_SECS.get(method)
        key = None
        if ttl:
            key = method + json.dumps(req.get("params"), sort_keys=True)
            hit = self.cache.get(key)
            if hit and hit[0] > time.monotonic():
                return {"jsonrpc": "2.0", "id": req.get("id"), "result": hit[1]}
        body = await self.forward({**req, "jsonrpc": "2.0"})
        if key and isinstance(body, dict) and "result" in body:
            self.cache[key] = (time.monotonic() + ttl, body["result"])
            if len(self.cache) > 5000:
                now = time.monotonic()
                self.cache = {k: v for k, v in self.cache.items() if v[0] > now}
        if isinstance(body, dict):
            body["id"] = req.get("id")
        return body

    async def rpc(self, request):
        if request.method == "GET" and request.headers.get("Upgrade", "").lower() == "websocket":
            return await self.websocket(request)
        if request.method == "OPTIONS":
            return web.Response(headers=cors())
        if request.method != "POST":
            return web.Response(status=405, text="POST JSON-RPC here\n", headers=cors())
        ip = self.client_ip(request)
        if request.content_length and request.content_length > MAX_BODY:
            return web.Response(status=413, headers=cors())
        try:
            payload = json.loads(await request.content.read(MAX_BODY + 1))
        except ValueError:
            return web.json_response({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "Parse error"}}, headers=cors())
        batch = isinstance(payload, list)
        reqs = payload if batch else [payload]
        if batch and (not reqs or len(reqs) > MAX_BATCH):
            return web.json_response({"jsonrpc": "2.0", "id": None, "error": {"code": -32600, "message": f"batches of 1 to {MAX_BATCH}"}}, headers=cors())
        if not self.allow(ip, len(reqs)):
            return web.json_response({"jsonrpc": "2.0", "id": None, "error": {"code": 429, "message": "Too many requests"}}, status=429, headers=cors())
        try:
            out = await asyncio.gather(*(self.call_one(r) for r in reqs))
        except RuntimeError as e:
            log.warning("no provider answered: %s", e)
            return web.json_response({"jsonrpc": "2.0", "id": None, "error": {"code": -32005, "message": "Solana providers are not answering; try again"}}, status=503, headers=cors())
        return web.json_response(out if batch else out[0], headers=cors())

    # ---- WebSocket
    async def websocket(self, request):
        ip = self.client_ip(request)
        if not self.allow(ip, 5):
            return web.Response(status=429, headers=cors())
        client = web.WebSocketResponse(heartbeat=30, max_msg_size=MAX_BODY)
        await client.prepare(request)
        upstream = None
        for p in self.ordered():
            if not p.ws:
                continue
            try:
                upstream = await self.session.ws_connect(p.ws, heartbeat=30, timeout=10, max_msg_size=16 * 1024 * 1024)
                p.served += 1
                break
            except (aiohttp.ClientError, asyncio.TimeoutError) as e:
                p.last_error = "ws " + type(e).__name__
        if upstream is None:
            await client.close(code=1013, message=b"no provider")
            return client

        async def pipe(src, dst, from_client):
            async for msg in src:
                if msg.type == aiohttp.WSMsgType.TEXT:
                    if from_client:
                        try:
                            m = json.loads(msg.data)
                            if isinstance(m, dict) and m.get("method") in DENIED or not self.allow(ip, 1):
                                continue
                        except ValueError:
                            continue
                    await dst.send_str(msg.data)
                elif msg.type == aiohttp.WSMsgType.BINARY:
                    await dst.send_bytes(msg.data)
                else:
                    break

        tasks = [asyncio.create_task(pipe(client, upstream, True)), asyncio.create_task(pipe(upstream, client, False))]
        await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for t in tasks:
            t.cancel()
        await upstream.close()
        await client.close()
        return client

    async def health(self, request):
        healthy = [p for p in self.providers if p.healthy]
        return web.json_response({"ok": bool(healthy), "providers": [p.status() for p in self.providers]},
                                 status=200 if healthy else 503, headers=cors())


def cors():
    return {"Access-Control-Allow-Origin": "*", "Access-Control-Allow-Methods": "POST, GET, OPTIONS",
            "Access-Control-Allow-Headers": "Content-Type, solana-client", "Access-Control-Max-Age": "86400"}


async def main():
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    path = os.environ.get("SOL_GATEWAY_CONFIG", "/etc/sol-gateway/config.json")
    with open(path) as f:
        cfg = json.load(f)
    gw = Gateway(cfg)
    gw.session = aiohttp.ClientSession(headers={"User-Agent": "monzero-sol-gateway/1"})
    app = web.Application(client_max_size=MAX_BODY)
    app.router.add_get("/health", gw.health)
    app.router.add_route("*", "/", gw.rpc)
    host, port = cfg.get("listen", "127.0.0.1:8899").rsplit(":", 1)
    runner = web.AppRunner(app, access_log=None)
    await runner.setup()
    await web.TCPSite(runner, host, int(port)).start()
    log.info("listening on %s:%s with %d providers", host, port, len(gw.providers))
    await gw.check_loop()


if __name__ == "__main__":
    asyncio.run(main())
