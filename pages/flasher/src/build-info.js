// Footer line reporting whether the page being viewed is the newest deploy.
// CI writes build-stamp.json into this directory on every publish; the check
// compares it against the latest commit touching pages/flasher.

const REPO = 'mobmesh/firmware';
const COMMITS_URL = `https://api.github.com/repos/${REPO}/commits?path=pages/flasher&per_page=1`;

function formatAgo(date) {
  let remaining = Math.max(0, Math.floor((Date.now() - date.getTime()) / 1000));
  const days = Math.floor(remaining / 86400);
  remaining -= days * 86400;
  const hours = Math.floor(remaining / 3600);
  remaining -= hours * 3600;
  const mins = Math.floor(remaining / 60);
  const secs = remaining - mins * 60;

  const unit = (n, label) => `${n} ${label}${n === 1 ? '' : 's'}`;
  const parts = [];
  if (days) parts.push(unit(days, 'day'));
  if (days || hours) parts.push(unit(hours, 'hr'));
  if (days || hours || mins) parts.push(unit(mins, 'min'));
  parts.push(unit(secs, 'sec'));
  return `${parts.join(' ')} ago`;
}

/**
 * Fill #build-info, or leave it blank. Best-effort: an unreachable API, a rate
 * limit or a missing stamp all end with an empty footer rather than an error.
 */
export async function loadBuildInfo(baseUrl = './') {
  const el = document.getElementById('build-info');
  if (!el) return;
  try {
    const [stampRes, apiRes] = await Promise.all([
      fetch(`${baseUrl}build-stamp.json`, { cache: 'no-store' }),
      fetch(COMMITS_URL),
    ]);
    if (!apiRes.ok) return;
    const [commit] = await apiRes.json();
    if (!commit) return;

    const date = new Date(commit.commit.committer.date);
    const sha = commit.sha.slice(0, 7);
    const current = () => {
      el.textContent = `Page is up to date (commit ${sha}, ${formatAgo(date)})`;
    };

    if (!stampRes.ok) {
      // No stamp: a local run, or a deploy from before CI wrote one.
      el.textContent = `Page last updated ${formatAgo(date)} (commit ${sha})`;
      return;
    }

    const stamp = await stampRes.json();
    if (stamp.sha === commit.sha) {
      current();
      return;
    }

    // Ancestry, not equality -- the deployed sha is usually later than the last
    // commit to touch this directory, and already contains it.
    const cmpRes = await fetch(
      `https://api.github.com/repos/${REPO}/compare/${commit.sha}...${stamp.sha}`,
    );
    const cmp = cmpRes.ok ? await cmpRes.json() : null;
    if (cmp && (cmp.status === 'ahead' || cmp.status === 'identical')) {
      current();
    } else {
      el.textContent =
        `This page is stale (built from ${stamp.sha.slice(0, 7)}, latest is ${sha}) -- try a hard refresh`;
      el.classList.add('stale');
    }
  } catch {
    // Best-effort only: leave the footer line blank.
  }
}
