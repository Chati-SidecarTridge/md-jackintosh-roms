// The site's views in one page: #/ (the landing page), #/build (build your
// image), #/how (how it works) and #/software (what runs). A view is a
// <section data-view>.

export const VIEWS = ['home', 'build', 'first', 'how', 'software'];

export function viewFromHash(hash) {
  const name = (hash || '').replace(/^#\/?/, '').split('/')[0];
  return VIEWS.includes(name) ? name : 'home';
}
