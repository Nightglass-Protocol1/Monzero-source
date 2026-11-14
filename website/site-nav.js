// Shared Menu button and submenus for the site header (every page).
(function () {
  "use strict";
  const button = document.querySelector(".site-header .menu-toggle");
  const nav = document.getElementById("site-nav");
  if (!nav) return;
  nav.classList.add("nav-js");
  const groups = Array.from(nav.querySelectorAll(".nav-group"));
  // Desktop CSS also opens submenus on hover/focus. Keep the announced state
  // aligned with what is visible, including when the viewport changes.
  const syncExpanded = () => requestAnimationFrame(() => {
    groups.forEach((group) => {
      const submenu = group.querySelector(".nav-sub");
      const toggle = group.querySelector(".nav-sub-toggle");
      if (submenu && toggle) toggle.setAttribute("aria-expanded", String(submenu.getClientRects().length > 0));
    });
  });
  const setGroup = (group, open) => {
    group.classList.toggle("open", open);
    group.classList.toggle("dismissed", !open && group.contains(document.activeElement));
    const toggle = group.querySelector(".nav-sub-toggle");
    if (toggle) toggle.setAttribute("aria-expanded", String(open));
  };
  const closeGroups = (except) => groups.forEach((g) => { if (g !== except) setGroup(g, false); });
  const close = () => {
    closeGroups();
    if (button) { nav.classList.remove("open"); button.setAttribute("aria-expanded", "false"); }
  };
  if (button) button.addEventListener("click", () => button.setAttribute("aria-expanded", String(nav.classList.toggle("open"))));
  groups.forEach((group) => {
    const toggle = group.querySelector(".nav-sub-toggle");
    if (!toggle) return;
    toggle.addEventListener("click", (event) => {
      event.stopPropagation();
      const open = !group.classList.contains("open");
      closeGroups(group);
      setGroup(group, open);
    });
    // Leaving a group with the keyboard closes it; Escape keeps it shut until the pointer or focus leaves.
    group.addEventListener("focusout", (event) => { if (!group.contains(event.relatedTarget)) { setGroup(group, false); group.classList.remove("dismissed"); } });
    group.addEventListener("mouseleave", () => group.classList.remove("dismissed"));
    group.addEventListener("focusin", () => group.classList.remove("dismissed"));
    for (const event of ["mouseenter", "mouseleave", "focusin", "focusout"]) {
      group.addEventListener(event, syncExpanded);
    }
  });
  window.addEventListener("resize", syncExpanded);
  nav.querySelectorAll("a").forEach((link) => link.addEventListener("click", close));
  document.addEventListener("click", (event) => { if (!nav.contains(event.target)) closeGroups(); });
  document.addEventListener("keydown", (event) => {
    if (event.key !== "Escape") return;
    const open = groups.find((g) => g.classList.contains("open") || g.contains(document.activeElement));
    if (open && window.matchMedia("(min-width: 1340px)").matches) {
      const toggle = open.querySelector(".nav-sub-toggle");
      if (toggle && open.contains(document.activeElement)) toggle.focus();
      setGroup(open, false);
      return;
    }
    const wasOpen = nav.classList.contains("open");
    close();
    if (wasOpen && button) button.focus();
  });
})();

// v1.0.0 upgrade banner (block 21,000): countdown before the fork, a short notice for a day after it,
// then nothing. Not in /admin or on /upgrade/ itself; "Hide" keeps it away for 24 hours.
(function () {
  "use strict";
  const FORK = 21000, AFTER = 720, KEY = "mzUpgradeHiddenUntil";
  const header = document.querySelector(".site-header");
  const nav = document.getElementById("site-nav");
  if (!header || !nav || nav.getAttribute("aria-label") !== "Primary navigation" || location.pathname.startsWith("/upgrade/")) return;
  try { if (Number(localStorage.getItem(KEY) || 0) > Date.now()) return; } catch { /* storage blocked: show it */ }
  fetch("/api/v1/network/upgrade/", { headers: { Accept: "application/json" } })
    .then((r) => (r.ok ? r.json() : null))
    .then((body) => {
      const d = body && body.data;
      if (!d || !Number.isInteger(d.height) || d.fork_height !== FORK || d.height >= FORK + AFTER) return;
      const bar = document.createElement("div");
      bar.className = "upgrade-banner" + (d.active ? " active" : "");
      bar.setAttribute("role", "region");
      bar.setAttribute("aria-label", "Network upgrade");
      const text = document.createElement("p");
      if (d.active) {
        text.textContent = "The v1.0.0 network upgrade is active. If your wallet stopped syncing, update it to v1.0.0; your coins are safe. ";
      } else {
        const days = Math.max(0, (d.estimated_at * 1000 - Date.now()) / 86400000);
        const when = days < 1 ? `about ${Math.max(1, Math.round(days * 24))} hours` : `about ${Math.round(days)} day${Math.round(days) === 1 ? "" : "s"}`;
        const strong = document.createElement("strong");
        strong.textContent = `Network upgrade in ${d.blocks_left.toLocaleString("en")} blocks (${when}). `;
        text.append(strong, "Update your wallet and node to v1.0.0 before block 21,000. Pool miners need to do nothing. ");
      }
      const link = document.createElement("a");
      link.href = "/upgrade/";
      link.textContent = "What to do";
      text.append(link);
      const hide = document.createElement("button");
      hide.type = "button";
      hide.className = "upgrade-hide";
      hide.textContent = "Hide";
      hide.setAttribute("aria-label", "Hide the upgrade notice for a day");
      hide.addEventListener("click", () => { bar.remove(); try { localStorage.setItem(KEY, String(Date.now() + 86400000)); } catch { /* fine */ } });
      bar.append(text, hide);
      // The landing page and guides have an in-content destination so a delayed
      // API reply cannot push the first screen down after it has been painted.
      const slot = document.querySelector("[data-upgrade-slot]");
      if (slot) slot.replaceChildren(bar);
      else header.before(bar);
    })
    .catch(() => {});
})();
