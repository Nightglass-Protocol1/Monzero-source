if (document.querySelector("#registration-form")) {
const form = document.querySelector('#registration-form');
const password = document.querySelector('#password');
const confirm = document.querySelector('#confirm');
for (const button of document.querySelectorAll('.password-toggle')) button.addEventListener('click', () => {
  const input = document.getElementById(button.getAttribute('aria-controls'));
  const show = input.type === 'password'; input.type = show ? 'text' : 'password';
  button.textContent = show ? 'Hide' : 'Show'; button.setAttribute('aria-pressed', String(show));
});
const match = () => confirm.setCustomValidity(confirm.value && password.value !== confirm.value ? 'Passwords must match.' : '');
password.addEventListener('input', match); confirm.addEventListener('input', match);
const signature = document.querySelector('#signature');
const preview = document.querySelector('#signature-preview');
function renderSignature() {
  // Render only formatting tags into text nodes; never interpret user HTML or URLs.
  preview.replaceChildren();
  const stack = [preview];
  for (const part of signature.value.split(/(\[\/?(?:b|i|u|s)\])/gi)) {
    const tag = /^\[(\/)?(b|i|u|s)\]$/i.exec(part);
    if (!tag) stack.at(-1).append(document.createTextNode(part));
    else if (!tag[1]) { const node = document.createElement(tag[2].toLowerCase()); stack.at(-1).append(node); stack.push(node); }
    else if (stack.length > 1 && stack.at(-1).tagName.toLowerCase() === tag[2].toLowerCase()) stack.pop();
  }
  if (!signature.value) preview.textContent = 'Your signature preview will appear here.';
}
function updateSignature() {
 document.querySelector('#signature-count').textContent = `${signature.value.length} / 300`;
 signature.setCustomValidity(signature.value.split('\n').length > 4 ? 'Use no more than 4 lines.' : (signature.value.match(/\[url(?:=|\])/gi) || []).length > 2 ? 'Use no more than 2 links.' : '');
 renderSignature();
}
signature.addEventListener('input', updateSignature);
for (const button of document.querySelectorAll('[data-tag]')) button.addEventListener('click', () => {
 const tag = button.dataset.tag, close = tag.split('=')[0];
 const value = `[${tag}]${signature.value.slice(signature.selectionStart, signature.selectionEnd) || 'text'}[/${close}]`;
 if (signature.value.length - (signature.selectionEnd-signature.selectionStart) + value.length > 300) return;
 signature.setRangeText(value, signature.selectionStart, signature.selectionEnd, 'select'); signature.focus(); updateSignature();
});
document.querySelector('#signature-preview-toggle').addEventListener('click', (event) => {
 preview.hidden = !preview.hidden; event.currentTarget.setAttribute('aria-expanded', String(!preview.hidden)); renderSignature();
});
const avatar = document.querySelector('#avatar');
let avatarURL;
function clearAvatar() { if (avatarURL) URL.revokeObjectURL(avatarURL); avatarURL = null; document.querySelector('#avatar-preview').textContent='MZ'; document.querySelector('#remove-avatar').hidden=true; }
avatar.addEventListener('change', () => {
 clearAvatar(); avatar.setCustomValidity(''); const file = avatar.files[0]; if (!file) return;
 if (!['image/jpeg','image/png','image/webp','image/gif'].includes(file.type) || file.size > 2*1024*1024) { avatar.setCustomValidity('Choose a JPG, PNG, WebP or GIF no larger than 2 MB.'); avatar.reportValidity(); return; }
 avatarURL = URL.createObjectURL(file); const image = new Image(); image.alt='Your avatar preview'; image.src=avatarURL;
 image.onerror = () => { clearAvatar(); avatar.setCustomValidity('This image could not be opened. Choose another file.'); avatar.reportValidity(); };
 document.querySelector('#avatar-preview').replaceChildren(image); document.querySelector('#remove-avatar').hidden=false;
});
document.querySelector('#remove-avatar').addEventListener('click', () => { clearAvatar(); avatar.value=''; avatar.setCustomValidity(''); });
updateSignature();

}
