// Copy buttons for the marketing kit. Clipboard writes happen only on a click.
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
