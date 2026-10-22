import { viewFromHash } from './router.js';

function showView() {
  const view = viewFromHash(window.location.hash);
  for (const section of document.querySelectorAll('[data-view]')) {
    section.hidden = section.dataset.view !== view;
  }
  for (const link of document.querySelectorAll('[data-nav]')) {
    link.classList.toggle('current', link.dataset.nav === view);
  }
  document.body.dataset.view = view;
  window.scrollTo(0, 0);
}

window.addEventListener('hashchange', showView);
showView();
