"""Generates website/marketing/{index.html,marketing.css,marketing.mjs} (community marketing kit)."""
import html
import pathlib

SITE = pathlib.Path.home() / "projects/Monzero-App/core-wt-website-ecosystem/website"
OUT = SITE / "marketing"
counter = [0]


def snip(text, label=None):
    """A copyable block. Text is shown exactly as it will be pasted."""
    counter[0] += 1
    sid = f"s{counter[0]}"
    head = f'<div class="snip-head"><span>{html.escape(label)}</span>' if label else '<div class="snip-head"><span></span>'
    return (f'<figure class="snip">{head}<button type="button" class="copy" data-copy="{sid}">Copy</button></div>'
            f'<pre id="{sid}">{html.escape(text.strip())}</pre></figure>')


def section(sid, title, intro, body):
    return f'<section id="{sid}"><h2>{title}</h2>{"<p>"+intro+"</p>" if intro else ""}{body}</section>'


def cards(items):
    return '<div class="grid">' + "".join(f'<div class="card"><h3>{t}</h3>{b}</div>' for t, b in items) + "</div>"


# ---------------------------------------------------------------- content
rules = cards([
    ("No price talk", "<p>Never predict, promise or discuss price, returns or \"gains\". XMZ has no market price yet. Redirect: <em>\"We talk about technology and adoption, not price.\"</em></p>"),
    ("Be accurate", "<p>Use the facts on this page and the <a href=\"/press/\">press kit</a>. If you are not sure, link the source instead of guessing. Say what is <strong>tested</strong>, not what is planned, as done.</p>"),
    ("Be honest about risk", "<p>The code is open source but not independently audited. The DEX is in testing. Say so when relevant; it builds trust.</p>"),
    ("Never ask for keys", "<p>No moderator or team member ever asks for a recovery phrase, private key or password, and never DMs first about support or airdrops.</p>"),
])

messages = (
    "<h3>Taglines</h3>"
    + snip("Private by design. Open by nature.", "Main tagline")
    + snip("Private, mined digital cash. Your keys, your coins.", "Alternative")
    + snip("Trade privately without handing your coins to anyone.", "DEX tagline")
    + "<h3>One-liner</h3>"
    + snip("Monzero (XMZ) is an independent, community-run privacy coin mined on ordinary CPUs, with no premine, no ICO and no token sale.")
    + "<h3>Short description (about 50 words)</h3>"
    + snip("Monzero (XMZ) is a private, community-run digital currency. Amounts are hidden with RingCT, senders are hidden in rings of 16, and every payment goes to a one-time address. It is mined with ordinary CPUs (RandomX), has no premine or token sale, and its wallets run on desktop, Android and in the browser.")
    + "<h3>Long description (about 120 words)</h3>"
    + snip("Monzero (XMZ) is an independent privacy currency built from the Monero codebase, with its own chain since 25 August 2026. Every transaction is private by default: RingCT hides amounts, ring signatures of 16 hide the sender, and one-time addresses hide the receiver. XMZ is mined, not sold: RandomX proof of work runs on ordinary CPUs, there was no ICO or token sale, and a permanent tail emission keeps miners paid. Wallets are available for Windows, Linux, Android and the browser (wallet.monzero.org), with signed releases anyone can verify. The project is building a decentralized exchange based on atomic swaps, so XMZ can be traded for Bitcoin and other coins without custodians, bridges or wrapped tokens. All source code is public.")
)

talking = cards([
    ("Privacy", "<ul><li>Private by default, not optional.</li><li>RingCT hides amounts; rings of 16 hide senders; one-time addresses hide receivers.</li><li>Built from the battle-tested Monero codebase.</li></ul>"),
    ("Fair mining", "<ul><li>RandomX: designed for ordinary CPUs, not ASICs.</li><li>Pool with 1% fee and hourly payouts, or mine solo.</li><li>Android miner app for phones.</li><li>A permanent 3.26 XMZ tail reward keeps the chain secure.</li></ul>"),
    ("No sale, full disclosure", "<ul><li>No premine, no ICO, no token sale, no developer fee.</li><li>The founder mined early blocks while the network was tested and publishes the holding and commitments (<a href=\"/founder/\">founder statement</a>).</li></ul>"),
    ("Your keys, your coins", "<ul><li>Web wallet: keys never leave the browser.</li><li>Desktop and Android wallets with your own 25-word recovery phrase.</li><li>Signed releases you can verify.</li></ul>"),
    ("DEX without custody", "<ul><li>Atomic swaps: either both sides get paid or both get refunded.</li><li>No exchange, custodian, bridge or wrapped token.</li><li>First real-funds XMZ ↔ BNB swap done; Bitcoin in mainnet testing.</li><li>Public trading opens in gated stages with small limits.</li></ul>"),
    ("Open development", "<ul><li>All code public on GitHub.</li><li>Test evidence published on the <a href=\"/roadmap/\">roadmap</a>.</li><li>Clear <a href=\"/objectives/\">objectives</a> and honest status.</li></ul>"),
])

say = """<div class="table-wrap"><table>
<tr><th>Say</th><th>Avoid</th><th>Why</th></tr>
<tr><td>Private, privacy coin</td><td>Anonymous, untraceable, impossible to track</td><td>Strong privacy, but no system is absolute; overclaims hurt trust.</td></tr>
<tr><td>No premine, no ICO; the founder mined early blocks and discloses the holding</td><td>Perfectly fair launch</td><td>The founder disclosure is public; be upfront about it.</td></tr>
<tr><td>The DEX is in testing; gated public beta next</td><td>DEX is live / trade now</td><td>Public trading is not open yet.</td></tr>
<tr><td>Open source, signed releases, not yet independently audited</td><td>Audited, 100% secure</td><td>No paid audit has been done.</td></tr>
<tr><td>Community rewards for real work, from a disclosed pool (being designed)</td><td>Free airdrop, claim now</td><td>No airdrop is live; fake ones are scams.</td></tr>
<tr><td>Mined with CPUs</td><td>Get rich mining</td><td>No promises of income.</td></tr>
<tr><td>(nothing about price)</td><td>Moon, 100x, cheap now, buy before</td><td>We never discuss price or returns.</td></tr>
</table></div>"""

x_posts = [
    ("Privacy", "\U0001F512 Private by default.\n\n$XMZ hides amounts with RingCT, senders in rings of 16, and receivers with one-time addresses. Nothing to switch on: every payment is private.\n\nmonzero.org\n\n#Monzero #Privacy #XMZ"),
    ("Mining", "⛏️ Mine $XMZ with the CPU you already have.\n\nRandomX proof of work, 1% pool fee, hourly payouts, or mine solo against your own node. There's even an Android miner.\n\nmonzero.org/mining\n\n#Monzero #CPUmining #RandomX"),
    ("Web wallet", "\U0001F310 The Monzero web wallet runs in your browser. Your keys never leave your device.\n\nSigned releases you can verify, auto-lock, no trackers. Try it with test coins first at testnet-wallet.monzero.org\n\nwallet.monzero.org\n\n#Monzero $XMZ #Privacy"),
    ("DEX", "⇄ Swap $XMZ for $BTC without handing your coins to anyone.\n\nAtomic swaps: either both sides get paid or both get refunded. No exchange, no custodian, no bridge, no wrapped token. In mainnet testing now.\n\nmonzero.org/roadmap\n\n#Monzero #AtomicSwap #DEX"),
    ("Fair launch", "✅ No premine. No ICO. No token sale. No developer fee.\n\nEvery $XMZ was mined. The founder's early mining is disclosed publicly with clear commitments.\n\nmonzero.org/founder\n\n#Monzero #CPUmining #Privacy"),
    ("Verify", "\U0001F50F Don't trust, verify.\n\nEvery Monzero release is signed. Check the signature and the SHA-256 before you run anything.\n\nmonzero.org/download\n\n#Monzero $XMZ #OpenSource"),
    ("Run a node", "\U0001F5A5️ Run a Monzero node and make the network stronger.\n\nOne command on Linux, then open port 6174. More independent nodes means a harder-to-attack chain.\n\nmonzero.org/download\n\n#Monzero $XMZ #RunANode"),
    ("Objectives", "\U0001F9ED Where $XMZ is going: wallets everywhere, a no-custody DEX opening in gated stages, and community rewards for real work.\n\nmonzero.org/objectives\n\n#Monzero #AtomicSwap #Privacy"),
]
xblock = "".join(snip(t, f"X post: {label}") for label, t in x_posts)

tg = (
    snip("""\U0001F44B Welcome to Monzero (XMZ)!

Private, mined, community-run digital cash.

\U0001F310 Website: monzero.org
\U0001F45B Wallets: monzero.org/download (web wallet: wallet.monzero.org)
⛏️ Mining: monzero.org/mining
\U0001F9ED Objectives: monzero.org/objectives
\U0001F4AC Forum: monzero.org/community/board

⚠️ Admins never DM you first and never ask for your recovery phrase, keys or passwords. There is no live airdrop and no Monzero token on other chains.""", "Telegram/Discord: welcome message")
    + snip("""⚠️ Scam warning

• Nobody from Monzero will ever DM you first or ask for your recovery phrase, private keys or passwords.
• There is NO live airdrop or giveaway. Anything asking you to "claim", "validate" or "sync your wallet" is a scam.
• Monzero has NO token on Solana, Ethereum, BNB Chain, Pump.fun or anywhere else. XMZ exists only on the Monzero blockchain.
• Only download wallets from monzero.org and use the web wallet only at wallet.monzero.org.

If in doubt, ask in the public chat. Report security issues privately to security@monzero.org.""", "Pinned scam warning")
    + snip("""\U0001F4E2 Monzero weekly update

✅ Shipped: [what was released or tested, with links]
\U0001F527 In progress: [what is being built]
\U0001F5D3️ Next: [what comes next]
\U0001F64C How to help: mine, run a node, test the web wallet with test coins, report bugs

Details: monzero.org/objectives · monzero.org/roadmap""", "Weekly update template")
)

forum = (
    snip("""Monzero (XMZ) is an independent privacy coin built from the Monero codebase, with its own chain since August 2026. It is mined with ordinary CPUs (RandomX), had no premine, ICO or token sale, and the founder's early mining is disclosed at monzero.org/founder. Wallets: desktop, Android and a signed web wallet (wallet.monzero.org). A no-custody atomic-swap DEX is in testing. Everything is open source: github.com/Nightglass-Protocol1/Monzero-source. Happy to answer questions; no price talk please.""", "Introduction for forums and Reddit (check each community's self-promotion rules first)")
    + snip("""Thanks for the question. XMZ isn't listed on exchanges yet, on purpose: listing waits until mining is spread out enough that the chain is hard to attack. A no-custody DEX (atomic swaps) is in testing, and the criteria and stages are public at monzero.org/objectives.""", "Reply: where can I buy?")
)

hashtags = snip("$XMZ #Monzero #Privacy #PrivacyCoin #CPUmining #RandomX #AtomicSwap #DEX #OpenSource", "Hashtags and cashtags (use at most five per post)")

faq = [
    ("Is Monzero a Monero fork?", "It is built from the Monero codebase, but it is an independent coin with its own chain from block 0 (25 August 2026), its own addresses, ports and emission. It shares no history or balances with Monero."),
    ("Was there a premine or ICO?", "No premine, no ICO, no token sale and no developer fee. The founder mined most early blocks while the network was being tested, before the public announcement; the holding and commitments are published at monzero.org/founder."),
    ("Where can I buy XMZ? What's the price?", "XMZ is not listed on exchanges yet and has no market price; we don't discuss price. Listing waits until mining is spread out enough. A no-custody DEX is in testing (monzero.org/roadmap)."),
    ("Is there an airdrop?", "No airdrop is live. A community rewards programme for real work (running nodes, testing, translations) is being designed and will be announced only on official channels. Anything asking you to claim tokens is a scam."),
    ("Is there an XMZ token on Solana / Ethereum / BNB Chain?", "No. XMZ exists only on the Monzero blockchain. Any token elsewhere claiming to be Monzero is not ours."),
    ("Which wallet should I use?", "The graphical wallet for Windows, the command-line wallet for Linux and Windows, the Android wallet, or the web wallet at wallet.monzero.org, all from monzero.org/download. Write down the 25-word recovery phrase on paper; the same phrase works in all of them."),
    ("How do I mine?", "With any modern CPU: join the pool (pool.monzero.org, 1% fee, hourly payouts) or mine solo against a node. Guides at monzero.org/mining."),
    ("Is the DEX live?", "Not for the public yet. Real-funds tests by the project have run on mainnet (XMZ ↔ BNB done, Bitcoin in progress). It will open in gated stages with small limits (monzero.org/objectives)."),
    ("Is it safe? Has it been audited?", "The code is open source and releases are signed, but it has not been independently audited yet. Use small amounts and report problems privately to security@monzero.org."),
    ("My mined coins are locked.", "Mined coins unlock after 60 blocks (about two hours). Pool payouts are sent hourly once you reach 1 XMZ."),
]
faqblock = "".join(snip(a, q) for q, a in faq)

modguide = cards([
    ("Scam patterns to remove", "<ul><li>DMs offering \"support\", \"recovery\" or \"wallet sync\".</li><li>Fake airdrops, giveaways, \"double your XMZ\".</li><li>Tokens on other chains using the Monzero name.</li><li>Links to look-alike domains (anything not monzero.org).</li><li>Impersonating admins (copied names and photos).</li></ul>"),
    ("Price and shilling", "<ul><li>Remove price predictions and pump talk.</li><li>Reply once with the price FAQ answer, then move on.</li><li>No paid promotion or referral links.</li></ul>"),
    ("Escalate", "<ul><li>Security issues: ask the reporter to email security@monzero.org privately; never discuss details publicly.</li><li>Lost funds: point to the recovery phrase restore guide; never ask for the phrase.</li><li>Press or exchange contact: point to the <a href=\"/press/\">press kit</a>.</li></ul>"),
])

colors = [("#0b1720", "Background"), ("#10242d", "Surface"), ("#3fdfe8", "Accent (teal)"), ("#a6f3f7", "Mint"), ("#edf3f7", "Text"), ("#edc48d", "Warning")]
swatches = '<div class="swatches">' + "".join(
    f'<div class="swatch"><span class="chip" data-color="{c}"></span><code>{c}</code><small>{n}</small></div>' for c, n in colors) + "</div>"

screens = [("1-welcome", "Welcome screen"), ("2-account", "Balance and accounts"), ("3-receive", "Receive with QR"), ("4-send", "Send"), ("5-transactions", "Transactions"), ("6-settings", "Settings")]
gallery = '<div class="gallery">' + "".join(
    f'<a class="shot" href="screens/monzero-wallet-{s}.jpg"><img src="screens/monzero-wallet-{s}-thumb.jpg" alt="Monzero desktop wallet: {n}" width="480" height="270" loading="lazy"><span>{n}</span></a>'
    for s, n in screens) + "</div>"

graphics = [
    ("Quote cards (1600 × 900, posts on X, Telegram, Discord)", [
        ("card-privacy-1600x900.jpg", "Private by design. Open by nature."),
        ("card-mining-1600x900.jpg", "Mine with the CPU you already have"),
        ("card-fair-1600x900.jpg", "No premine. No ICO. No token sale."),
        ("card-dex-1600x900.jpg", "Swap without handing your coins to anyone"),
        ("card-wallet-1600x900.jpg", "Your keys never leave your device"),
        ("card-scam-1600x900.jpg", "Scam warning: never share your recovery phrase")]),
    ("Banners and headers", [
        ("x-header-1500x500.jpg", "X / Twitter header, 1500 × 500"),
        ("youtube-banner-2560x1440.jpg", "YouTube channel art, 2560 × 1440"),
        ("linkedin-banner-1584x396.jpg", "LinkedIn banner, 1584 × 396"),
        ("discord-banner-960x540.jpg", "Discord server banner, 960 × 540"),
        ("discord-splash-1920x1080.jpg", "Discord invite background, 1920 × 1080"),
        ("post-community-1600x900.jpg", "Community post image, 1600 × 900"),
        ("post-community-1200x675.jpg", "Community post image, 1200 × 675"),
        ("link-preview-1200x630.jpg", "Link preview / Open Graph, 1200 × 630")]),
    ("Profile pictures and squares", [
        ("avatar-coin-400.jpg", "Profile picture: coin, 400 × 400"),
        ("avatar-coin-1000.jpg", "Profile picture: coin, 1000 × 1000"),
        ("avatar-emblem-400.jpg", "Profile picture: emblem, 400 × 400"),
        ("square-emblem-1080.jpg", "Square post, 1080 × 1080"),
        ("coin-transparent-512.png", "Coin, transparent PNG, 512 × 512"),
        ("emblem-transparent-512.png", "Emblem, transparent PNG, 512 × 512")]),
    ("Stories and vertical", [
        ("story-1080x1920.jpg", "Story / Status / Reel cover, 1080 × 1920")]),
]
library = ('<p><a class="button-dl" href="monzero-marketing-graphics.zip" download>Download everything (ZIP, about 10 MB)</a> '
           '<span class="muted">All graphics below, the press-kit logos and banners, and the app screenshots.</span></p>')
for title, items in graphics:
    library += f"<h3>{title}</h3><div class=\"gallery\">" + "".join(
        f'<a class="shot" href="graphics/{f}" download><img src="graphics/thumbs/{f.rsplit(".", 1)[0]}.jpg" alt="{html.escape(n)}" loading="lazy"><span>{html.escape(n)}</span></a>'
        for f, n in items) + "</div>"
library += "<p class=\"muted\">Made from the official artwork. Please do not crop the logo, recolour it or add other coins' logos to these images.</p>"

visuals = (
    "<h3>Logos and banners</h3><p>Use the official files from the <a href=\"/press/#logos\">press kit</a>: emblem, coin logo, wordmark and banners in several sizes. Do not recolour, stretch or add effects to the logo.</p>"
    '<div class="logos"><img src="/press/monzero-emblem-20261026-200.png" alt="Monzero emblem" width="100" height="100"><img src="/press/monzero-xmz-logo-200.png" alt="Monzero XMZ coin logo" width="100" height="100"></div>'
    "<h3>Brand colours</h3>" + swatches
    + "<h3>App screenshots</h3><p>Desktop wallet (Microsoft Store edition, empty demo wallet). Click for full size (1600 px).</p>" + gallery
    + "<p class=\"muted\">Never post a screenshot that shows a real recovery phrase, private key or a wallet you use.</p>"
)

links = """<div class="table-wrap"><table>
<tr><th>What</th><th>Official link</th></tr>
<tr><td>Website</td><td>https://monzero.org</td></tr>
<tr><td>Downloads and release verification</td><td>https://monzero.org/download/</td></tr>
<tr><td>Web wallet (real coins)</td><td>https://wallet.monzero.org</td></tr>
<tr><td>Web wallet (test coins)</td><td>https://testnet-wallet.monzero.org</td></tr>
<tr><td>Mining guide</td><td>https://monzero.org/mining/</td></tr>
<tr><td>Mining pool</td><td>https://pool.monzero.org</td></tr>
<tr><td>Block explorer</td><td>https://monzero.org/explorer/</td></tr>
<tr><td>Objectives</td><td>https://monzero.org/objectives/</td></tr>
<tr><td>DEX roadmap and test evidence</td><td>https://monzero.org/roadmap/</td></tr>
<tr><td>Founder holdings statement</td><td>https://monzero.org/founder/</td></tr>
<tr><td>Community forum</td><td>https://monzero.org/community/board/</td></tr>
<tr><td>Press and listing kit</td><td>https://monzero.org/press/</td></tr>
<tr><td>Source code</td><td>https://github.com/Nightglass-Protocol1/Monzero-source</td></tr>
<tr><td>X</td><td>https://x.com/MonzeroXMZ</td></tr>
<tr><td>Security reports (private)</td><td>security@monzero.org</td></tr>
</table></div>"""

calendar = """<div class="table-wrap"><table>
<tr><th>Day</th><th>Theme</th><th>Example</th></tr>
<tr><td>Monday</td><td>Weekly update</td><td>What shipped and what is next (template above)</td></tr>
<tr><td>Tuesday</td><td>Privacy explainer</td><td>How RingCT, rings and one-time addresses work</td></tr>
<tr><td>Wednesday</td><td>Mining</td><td>CPU tips, pool stats, Android miner</td></tr>
<tr><td>Thursday</td><td>DEX progress</td><td>Test results with transaction links</td></tr>
<tr><td>Friday</td><td>Wallets and safety</td><td>Web wallet, verifying releases, scam warning</td></tr>
<tr><td>Weekend</td><td>Community</td><td>Node runners, translations, questions answered</td></tr>
</table></div>"""

nav_items = [("rules", "Rules"), ("messages", "Key messages"), ("talking", "Talking points"), ("words", "Say / avoid"),
             ("posts", "Ready-to-post"), ("faq", "FAQ answers"), ("mods", "Moderator guide"), ("graphics", "Graphics"), ("visuals", "Logos & screenshots"),
             ("links", "Official links"), ("calendar", "Content plan")]

body = (
    '<p class="eyebrow">Community / Marketing kit</p><h1>Community marketing kit</h1>'
    '<p class="lead">Everything moderators, ambassadors and marketers need to talk about Monzero accurately: key messages, ready-to-post copy, answers to common questions, scam warnings, a graphics library sized for every platform, and official links. Press the <strong>Copy</strong> button on any block.</p>'
    '<nav class="pills">' + "".join(f'<a href="#{i}">{t}</a>' for i, t in nav_items) + "</nav>"
    + section("rules", "The four rules", "Read these first. They protect the community and the project.", rules)
    + section("messages", "Key messages", "Use these as they are, or adapt them while keeping the facts.", messages)
    + section("talking", "Talking points", "The points to make, by topic, with the page to link.", talking)
    + section("words", "Say this, not that", "", say)
    + section("posts", "Ready-to-post library", "Check the facts are still current before posting (the <a href=\"/objectives/\">objectives</a> page is updated as work ships).",
              "<h3>X / Twitter</h3>" + xblock + "<h3>Telegram and Discord</h3>" + tg + "<h3>Forums and Reddit</h3>" + forum + "<h3>Hashtags</h3>" + hashtags)
    + section("faq", "FAQ answers for moderators", "Copy the answer that fits; link the source page where you can.", faqblock)
    + section("mods", "Moderator guide", "", modguide)
    + section("graphics", "Graphics library", "Ready-sized images for every platform. Click any image to download it.", library)
    + section("visuals", "Logos, colours and screenshots", "", visuals)
    + section("links", "Official links", "Only share these. Anything else claiming to be Monzero is not official.", links)
    + section("calendar", "Weekly content plan", "A simple rhythm for channels that post every day.", calendar)
    + '<p class="obj-footnote">Community marketing kit · last updated 3 November 2026 · questions to the moderators on the <a href="/community/board/">forum</a>.</p>'
)

road = (SITE / "roadmap/index.html").read_text().splitlines()
header = [line for line in road if 'class="site-header"' in line][0]
page = f'''<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#0b1118">
  <meta name="description" content="Monzero community marketing kit for moderators, ambassadors and marketers: key messages, ready-to-post copy, FAQ answers, scam warnings, brand colours, screenshots and official links.">
  <link rel="icon" href="../assets/monzero-emblem-20261026-96.webp" type="image/webp">
  <title>Marketing Kit · Monzero</title><link rel="canonical" href="https://monzero.org/marketing/"><meta property="og:type" content="website"><meta property="og:site_name" content="Monzero"><meta property="og:url" content="https://monzero.org/marketing/"><meta property="og:title" content="Monzero community marketing kit"><meta property="og:description" content="Key messages, ready-to-post copy, FAQ answers and visuals for the Monzero community.">
  <link rel="stylesheet" href="../site-nav.css?v=20261026-brand">
  <link rel="stylesheet" href="/site-shell.css?v=20261026-brand"><noscript><link rel="stylesheet" href="/no-script.css?v=20261026-brand"></noscript>
  <link rel="stylesheet" href="../objectives/objectives.css?v=20261103">
  <link rel="stylesheet" href="marketing.css?v=20261103">
</head>
<body data-page="marketing" class="site-shell">
  <a class="skip" href="#main">Skip to the marketing kit</a>
{header}
  <main tabindex="-1" id="main" class="obj mk">
{body}
  </main>
  <footer class="footer wrap"><a class="brand" href="../"><img class="brand-mark brand-coin" src="../assets/monzero-emblem-20261026-96.webp" alt="" width="32" height="32"> MONZERO</a><p>Experimental, community-run development.<br>Marketing kit · updated 3 November 2026.</p><a href="#main">Back to top ↑</a></footer>
  <script type="module" src="marketing.mjs?v=20261103"></script><script src="../site-nav.js?v=20261026-brand" defer></script>
</body>
</html>
'''

css = """/* Community marketing kit: builds on ../objectives/objectives.css (.obj) */
.mk .eyebrow { color: var(--accent); font-size: .85rem; letter-spacing: .08em; text-transform: uppercase; margin: 0 0 6px; }
.mk .pills { display: flex; flex-wrap: wrap; gap: 8px; margin: 18px 0 4px; }
.mk .pills a { color: var(--accent); background: var(--accent-soft); text-decoration: none; padding: 4px 10px; border-radius: 999px; font-size: .9rem; }
.mk a { color: var(--accent); }
.mk .snip { margin: 10px 0 14px; background: var(--surface); border: 1px solid var(--line); border-radius: 10px; overflow: hidden; }
.mk .snip-head { display: flex; justify-content: space-between; align-items: center; gap: 8px; padding: 6px 10px; background: var(--accent-soft); font-size: .85rem; color: var(--muted); }
.mk .snip pre { margin: 0; padding: 12px 14px; white-space: pre-wrap; word-break: break-word; font: .95rem/1.5 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; color: var(--text); }
.mk .copy { border: 1px solid var(--accent); background: transparent; color: var(--accent); border-radius: 6px; padding: 3px 12px; font: inherit; font-size: .85rem; cursor: pointer; min-height: 30px; }
.mk .copy:hover, .mk .copy:focus-visible { background: var(--accent); color: var(--bg); }
.mk .copy.done { background: var(--success); border-color: var(--success); color: var(--bg); }
.mk .swatches { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 10px; }
.mk .swatch { background: var(--surface); border: 1px solid var(--line); border-radius: 10px; padding: 10px; display: grid; gap: 4px; }
.mk .chip { height: 44px; border-radius: 6px; border: 1px solid var(--line); }
.mk .chip[data-color="#0b1720"] { background: #0b1720; } .mk .chip[data-color="#10242d"] { background: #10242d; }
.mk .chip[data-color="#3fdfe8"] { background: #3fdfe8; } .mk .chip[data-color="#a6f3f7"] { background: #a6f3f7; }
.mk .chip[data-color="#edf3f7"] { background: #edf3f7; } .mk .chip[data-color="#edc48d"] { background: #edc48d; }
.mk .swatch small { color: var(--muted); }
.mk .logos { display: flex; gap: 14px; flex-wrap: wrap; margin: 8px 0; }
.mk .logos img { width: 100px; height: 100px; object-fit: contain; background: var(--surface); border: 1px solid var(--line); border-radius: 10px; padding: 8px; }
.mk .gallery { display: grid; grid-template-columns: repeat(auto-fill, minmax(200px, 1fr)); gap: 12px; }
.mk .shot { display: block; text-decoration: none; background: var(--surface); border: 1px solid var(--line); border-radius: 10px; overflow: hidden; }
.mk .shot img { display: block; width: 100%; height: auto; }
.mk .shot span { display: block; padding: 8px 10px; color: var(--text); font-size: .9rem; }
.mk td { word-break: break-word; }
.mk .button-dl { display: inline-block; background: var(--accent); color: var(--bg); font-weight: 700; text-decoration: none; padding: 10px 16px; border-radius: 8px; margin: 4px 8px 8px 0; }
.mk .button-dl:hover, .mk .button-dl:focus-visible { background: var(--mint); }
"""

js = """// Copy buttons for the marketing kit. Clipboard writes happen only on a click.
for (const button of document.querySelectorAll('button[data-copy]')) {
  button.addEventListener('click', async () => {
    const source = document.getElementById(button.dataset.copy);
    if (!source) return;
    try {
      await navigator.clipboard.writeText(source.textContent);
      button.textContent = 'Copied';
      button.classList.add('done');
    } catch {
      const range = document.createRange();
      range.selectNodeContents(source);
      const selection = window.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
      button.textContent = 'Selected: press Ctrl+C';
    }
    setTimeout(() => { button.textContent = 'Copy'; button.classList.remove('done'); }, 2500);
  });
}
"""

OUT.mkdir(exist_ok=True)
(OUT / "index.html").write_text(page)
(OUT / "marketing.css").write_text(css)
(OUT / "marketing.mjs").write_text(js)
print("snippets:", counter[0], "page bytes:", len(page))
