"""Maintain the same header and shared styles on every static public page.

  python3 -I utils/website/nav_submenus.py          # update pages
  python3 -I utils/website/nav_submenus.py --check  # verify without writing

website/lib/site-nav-submenus.json is the single navigation definition. The PHP
header reads it directly; this script renders it into static HTML. Page content,
tool controls, metadata and scripts are preserved.
"""
import html
import json
import re
import sys
from pathlib import Path

WEBSITE = Path(__file__).resolve().parents[2] / "website"
REVISION = "20261026-brand"
HEADER = re.compile(r'<header\b(?=[^>]*\bclass="[^"]*\bsite-header\b[^"]*")[^>]*>.*?</header>', re.S)


def page_path(file):
    rel = file.relative_to(WEBSITE).as_posix()
    return "/" if rel == "index.html" else "/" + (rel[:-10] if rel.endswith("index.html") else rel)


def section_matches(href, path):
    return href.startswith("/") and "#" not in href and "?" not in href and (
        href == path or (href != "/" and href.endswith("/") and path.startswith(href)))


def render_navigation(config, path):
    out = []
    for href, label in config["links"]:
        subs = config["submenus"].get(label, [])
        current = ' aria-current="page"' if href == path else ""
        top = f'<a href="{html.escape(href, quote=True)}"{current}>{html.escape(label)}</a>'
        if not subs:
            out.append(top)
            continue
        group = 'nav-group current' if any(section_matches(h, path) for h, _ in subs) else 'nav-group'
        sid = "nav-sub-" + re.sub(r"[^a-z]+", "-", label.lower()).strip("-")
        # Announce the current page once, on its top-level link when it has one.
        items = "".join(f'<a href="{html.escape(h, quote=True)}"' +
                        (' aria-current="page"' if h == path and not current else '') +
                        f'>{html.escape(title)}</a>' for h, title in subs)
        out.append(f'<div class="{group}">{top}<button class="nav-sub-toggle" type="button" aria-expanded="false" aria-controls="{sid}">'
                   f'<span class="visually-hidden">{html.escape(label)} pages</span></button><div class="nav-sub" id="{sid}">{items}</div></div>')
    return ''.join(out)


def render_header(config, path):
    return ('<header class="site-header"><a class="brand" href="/" aria-label="Monzero home">'
            '<img class="brand-mark brand-coin" src="/assets/monzero-emblem-20261026-96.webp" alt="" width="32" height="32"><span>MONZERO</span></a>'
            '<button class="menu-toggle" type="button" aria-expanded="false" aria-controls="site-nav">Menu</button>'
            '<nav id="site-nav" aria-label="Primary navigation">' + render_navigation(config, path) + '</nav>'
            '<a class="header-cta" href="/#get-started">Get started</a></header>')


def shared_shell(text):
    def body(match):
        tag = match[0]
        classes = re.search(r'\bclass="([^"]*)"', tag)
        if classes:
            if 'site-shell' not in classes[1].split():
                tag = tag[:classes.start(1)] + classes[1] + ' site-shell' + tag[classes.end(1):]
        else:
            tag = tag[:-1] + ' class="site-shell">'
        return tag
    text = re.sub(r'<body\b[^>]*>', body, text, count=1)
    # Make the native skip link focus the content with JavaScript disabled too.
    text = re.sub(r'<main\b(?![^>]*\bid=)', '<main id="main"', text, count=1)
    if not re.search(r'<a\b[^>]*class="[^"]*\bskip(?:-link)?\b', text):
        text = re.sub(r'(<body\b[^>]*>)', r'\1<a class="skip-link" href="#main">Skip to content</a>', text, count=1)
    text = re.sub(r'<main\b(?=[^>]*\bid="main")(?![^>]*\btabindex=)', '<main tabindex="-1"', text, count=1)
    text = re.sub(r'<noscript>\s*<link[^>]*href="/no-script\.css[^\"]*"[^>]*>\s*</noscript>\s*', '', text)
    text = re.sub(r'<link[^>]*href="/site-shell\.css[^\"]*"[^>]*>\s*', '', text)
    text = re.sub(r'(href="[^\"]+\.css)(?:\?[^\"]*)?(")', rf'\1?v={REVISION}\2', text)
    text = re.sub(r'(src="[^\"]*site-nav\.js)(?:\?[^\"]*)?(")', rf'\1?v={REVISION}\2', text)
    styles = f'<link rel="stylesheet" href="/site-shell.css?v={REVISION}"><noscript><link rel="stylesheet" href="/no-script.css?v={REVISION}"></noscript>'
    return text.replace('</head>', styles + '</head>', 1)


def main():
    check = '--check' in sys.argv[1:]
    config = json.loads((WEBSITE / 'lib/site-nav-submenus.json').read_text())
    stale = []
    for file in sorted(WEBSITE.rglob('*.html')):
        text = file.read_text(encoding='utf-8')
        match = HEADER.search(text)
        if not match or 'aria-label="Primary navigation"' not in match[0]:
            continue
        new = text[:match.start()] + render_header(config, page_path(file)) + text[match.end():]
        new = shared_shell(new)
        if new != text:
            stale.append(file.relative_to(WEBSITE).as_posix())
            if not check:
                file.write_text(new, encoding='utf-8')
    if check and stale:
        sys.exit('navigation/shared styles out of date: ' + ', '.join(stale))
    print('navigation and shared styles up to date' if not stale else 'updated: ' + ', '.join(stale))


if __name__ == '__main__':
    main()
