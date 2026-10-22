// The site's three views in one page: #/ (the landing page), #/build (build
// your image) and #/how (how it works). A view is a <section data-view>.

export const VIEWS = ['home', 'build', 'how'];

export function viewFromHash(hash) {
  const name = (hash || '').replace(/^#\/?/, '').split('/')[0];
  return VIEWS.includes(name) ? name : 'home';
}
