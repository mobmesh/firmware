// Capability check: is Web Serial usable, and if not, why. Browser kind is
// best-effort and only tailors the failure copy.

/** @typedef {'supported'|'insecure'|'unsupported'} SerialSupport */
/** @typedef {'ios'|'firefox'|'safari'|'chromium'|'unknown'} BrowserKind */

/** @returns {SerialSupport} */
export function detectSerialSupport() {
  if ('serial' in navigator) return 'supported';
  if (!window.isSecureContext) return 'insecure';
  return 'unsupported';
}

/** @returns {BrowserKind} */
export function detectBrowserKind() {
  const ua = navigator.userAgent || '';

  // Every iOS browser is WebKit underneath, so a UA saying "Chrome" must not reach
  // the chromium bucket. iPadOS reports MacIntel, hence the touch-point test.
  const isIOS =
    /iPad|iPhone|iPod/.test(ua) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
  if (isIOS) return 'ios';

  if (/Firefox\//.test(ua) && !/Seamonkey/.test(ua)) return 'firefox';
  if (/Safari/.test(ua) && !/Chrome|Chromium|Edg|OPR|Brave/.test(ua)) return 'safari';
  if (/Chrome|Chromium|Edg|OPR|Brave/.test(ua)) return 'chromium';
  return 'unknown';
}

const UNSUPPORTED_COPY = {
  firefox: {
    title: 'Firefox can’t run the flasher',
    body:
      'Firefox doesn’t ship the Web Serial API the flasher needs to talk to your board over USB. ' +
      'Open this same page in a Chromium-based browser to continue.',
    suggestion: 'Chrome, Edge, Brave, Arc, Vivaldi, or Opera will all work.',
  },
  safari: {
    title: 'Safari can’t run the flasher',
    body: 'Safari doesn’t expose the Web Serial API. Open this page in a Chromium-based browser instead.',
    suggestion: 'Chrome, Edge, Brave, Arc, or Vivaldi all work on macOS.',
  },
  ios: {
    title: 'Web Serial isn’t available on iOS',
    body:
      'Every browser on iPhone and iPad uses Apple’s WebKit, which doesn’t support Web Serial — even ' +
      'Chrome, Edge and Brave for iOS. Flashing needs a desktop computer.',
    suggestion: 'Open this URL on a Mac, Windows, or Linux machine in Chrome, Edge, or Brave.',
  },
  // Chromium with no serial access means a webview, IDE preview, iframe or stripped
  // build. Telling this user to "use Chrome" tells them to do what they already are.
  chromium: {
    title: 'This window can’t reach Web Serial',
    body:
      'This looks like a Chromium-based browser, but the Web Serial API isn’t available here. That’s ' +
      'normal inside embedded previews (VS Code, Discord, in-app browsers) and some stripped-down Linux ' +
      'Chromium packages.',
    suggestion: 'Open this URL in a normal Chrome, Edge, or Brave tab.',
  },
  unknown: {
    title: 'Browser not compatible',
    body:
      'This browser doesn’t expose the Web Serial API. Open the page in a normal Chrome, Edge, or Brave ' +
      'window — not an embedded preview or in-app browser.',
    suggestion: 'Chrome, Edge, Brave, Arc, Vivaldi, or Opera all work.',
  },
};

const INSECURE_COPY = {
  title: 'This page isn’t running securely',
  body:
    'Web Serial is only available over HTTPS or on localhost. This page was served over plain HTTP, so the ' +
    'browser blocks USB access.',
  suggestion: 'Open the https:// version of this URL.',
};

// Null when the browser can proceed; otherwise the copy to show instead of the wizard.
export function compatibilityCopy(support, browserKind) {
  if (support === 'supported') return null;
  if (support === 'insecure') return INSECURE_COPY;
  return UNSUPPORTED_COPY[browserKind] ?? UNSUPPORTED_COPY.unknown;
}

/** Only reached once Web Serial already works, so the browser is already Chromium —
 * this is never checked on Firefox/Safari/iOS. */
export function detectFileSystemAccessSupport() {
  return typeof window !== 'undefined' && typeof window.showSaveFilePicker === 'function';
}

// Brave is the one Chromium browser that ships this disabled by default — a privacy
// decision on their end, not a bug — and its UA string doesn't self-identify (see
// `detectBrowserKind`, which folds it into 'chromium'), so the copy names both causes
// rather than guessing which one applies.
const FILE_SYSTEM_ACCESS_COPY = {
  title: 'This browser can’t save the bootloader file directly',
  body:
    'Writing the bootloader update needs the File System Access API, which is on by default in ' +
    'Chrome and Edge but is disabled by default in Brave as a privacy setting.',
  suggestion:
    'If you’re on Brave: open brave://flags/#file-system-access-api, set it to Enabled, and relaunch. ' +
    'Otherwise, try this page in Chrome or Edge.',
};

/** Null when the picker will work; otherwise the copy to show before asking for a
 * double-tap the browser can't follow through on. */
export function fileSystemAccessCopy() {
  return detectFileSystemAccessSupport() ? null : FILE_SYSTEM_ACCESS_COPY;
}
