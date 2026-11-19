#!/usr/bin/env python3
"""Tell IndexNow search engines (Bing, Yandex, Seznam, Naver…) that monzero.org's pages changed.

    python3 utils/website/indexnow.py            every URL in the live sitemap
    python3 utils/website/indexnow.py URL ...    just these URLs

The key is the 32-hex-character file website/<key>.txt, published at https://monzero.org/<key>.txt;
the engines fetch it to check the site is ours. No account is needed. Google does not use IndexNow:
it reads the sitemap named in robots.txt (or use Search Console).
"""
import json, re, sys, urllib.request
from pathlib import Path

HOST = "monzero.org"
SITE = Path(__file__).resolve().parents[2] / "website"

def main():
    keys = [p.stem for p in SITE.glob("*.txt") if re.fullmatch(r"[0-9a-f]{32}", p.stem) and p.read_text().strip() == p.stem]
    if len(keys) != 1:
        sys.exit(f"expected one IndexNow key file in {SITE}, found {len(keys)}")
    key = keys[0]
    live = urllib.request.urlopen(f"https://{HOST}/{key}.txt", timeout=20).read().decode().strip()
    if live != key:
        sys.exit(f"https://{HOST}/{key}.txt is not live yet: deploy the website first")
    urls = sys.argv[1:] or re.findall(r"<loc>(https://monzero\.org/[^<]*)</loc>",
                                      urllib.request.urlopen(f"https://{HOST}/sitemap.xml", timeout=20).read().decode())
    if not urls:
        sys.exit("no URLs to submit")
    body = json.dumps({"host": HOST, "key": key, "keyLocation": f"https://{HOST}/{key}.txt", "urlList": urls}).encode()
    req = urllib.request.Request("https://api.indexnow.org/indexnow", body, {"Content-Type": "application/json; charset=utf-8"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            print(f"IndexNow: {r.status} for {len(urls)} URLs (200/202 = accepted)")
    except urllib.error.HTTPError as e:
        sys.exit(f"IndexNow refused: {e.code} {e.read().decode(errors='replace')[:300]}")

if __name__ == "__main__":
    main()
