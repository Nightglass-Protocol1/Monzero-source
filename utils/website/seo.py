"""Search and link-preview basics for every public page, plus sitemap.xml and robots.txt.

  python3 -I utils/website/seo.py            # update pages, sitemap.xml and robots.txt
  python3 -I utils/website/seo.py --check    # exit 1 if anything is out of date

Each public page (one with the primary navigation) gets a canonical link and Open Graph / Twitter
tags built from its own <title> and meta description. A page's existing og:title and
og:description (the homepage's wording) are kept. Re-running it is idempotent.
"""
import html, re, sys
from pathlib import Path
from html.parser import HTMLParser

WEBSITE = Path(__file__).resolve().parents[2] / "website"
SITE = "https://monzero.org"
IMAGE = (f"{SITE}/assets/monzero-share-20261026-1200x630.jpg", 1200, 630, "Monzero MZ emblem and wordmark: Join the Monzero community. Privacy focused, independent blockchain, CPU mineable.")
SKIP = {"/mining/benchmarks/submission.html"}   # a form helper, not a page to list
BEGIN, END = "<!-- seo:begin -->", "<!-- seo:end -->"
OG = re.compile(r'\s*<meta\b(?=[^>]*\b(?:property="og:[^"]+"|name="twitter:[^"]+"))[^>]*>|\s*<link\b(?=[^>]*\brel="canonical")[^>]*>')


def page_url(file):
    rel = file.relative_to(WEBSITE).as_posix()
    return "/" if rel == "index.html" else "/" + (rel[:-len("index.html")] if rel.endswith("/index.html") else rel)


def meta(text, pattern):
    m = re.search(pattern, text, re.S)
    return html.unescape(m.group(1).strip()) if m else None


class Metadata(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.values = {}
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        if tag == "meta":
            attrs = dict(attrs)
            self.values[attrs.get("property") or attrs.get("name")] = attrs.get("content", "")


def block(text, url):
    tags = Metadata(text).values
    title = tags.get("og:title") or meta(text, r"<title>(.*?)</title>") or "Monzero"
    desc = tags.get("og:description") or tags.get("description") or ""
    a = lambda v: html.escape(v, quote=True)
    lines = [f'<link rel="canonical" href="{SITE}{url}">', '<meta property="og:type" content="website">', '<meta property="og:site_name" content="Monzero">',
             f'<meta property="og:url" content="{SITE}{url}">', f'<meta property="og:title" content="{a(title)}">', f'<meta property="og:description" content="{a(desc)}">',
             f'<meta property="og:image" content="{IMAGE[0]}">', f'<meta property="og:image:width" content="{IMAGE[1]}">', f'<meta property="og:image:height" content="{IMAGE[2]}">',
             f'<meta property="og:image:alt" content="{a(IMAGE[3])}">', '<meta name="twitter:card" content="summary_large_image">',
             f'<meta name="twitter:title" content="{a(title)}">', f'<meta name="twitter:description" content="{a(desc)}">', f'<meta name="twitter:image" content="{IMAGE[0]}">']
    return BEGIN + "".join(lines) + END


def update(text, url):
    seo = block(text, url)   # read the page's own og:title/description before removing old tags
    base = OG.sub("", re.sub(re.escape(BEGIN) + ".*?" + re.escape(END), "", text, flags=re.S))
    return base.replace("</title>", "</title>" + seo, 1)


def main():
    check = "--check" in sys.argv[1:]
    stale, urls = [], []
    for file in sorted(WEBSITE.rglob("*.html")):
        text = file.read_text(encoding="utf-8")
        if 'aria-label="Primary navigation"' not in text and file.name != "index.html":
            continue
        url = page_url(file)
        if url in SKIP or "/admin/" in url or 'aria-label="Primary navigation"' not in text:
            continue
        urls.append(url)
        new = update(text, url)
        if new != text:
            stale.append(url)
            if not check:
                file.write_text(new, encoding="utf-8")
    extra = ["/faucet/", "/community/voting/"]   # PHP pages
    sitemap = '<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n' + "".join(
        f"  <url><loc>{SITE}{u}</loc></url>\n" for u in sorted(set(urls + extra))) + "</urlset>\n"
    robots = "User-agent: *\nDisallow: /admin/\nDisallow: /api/\nAllow: /api/v1/supply/\n\nSitemap: https://monzero.org/sitemap.xml\n"
    for name, content in [("sitemap.xml", sitemap), ("robots.txt", robots)]:
        path = WEBSITE / name
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            stale.append(name)
            if not check:
                path.write_text(content, encoding="utf-8")
    if check and stale:
        sys.exit("seo out of date: " + ", ".join(stale))
    print("seo up to date" if not stale else f"updated {len(stale)}: " + ", ".join(stale))


if __name__ == "__main__":
    main()
