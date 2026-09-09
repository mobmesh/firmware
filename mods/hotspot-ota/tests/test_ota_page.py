"""Browser cases for the OTA page's file-selection state machine.

Drives the shipping ota-page.min.html in Chromium against mock_server, which can be told
to fail an upload -- the failure paths are the ones a working node never produces.
"""
import glob
import os
import pathlib
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import images                                    # noqa: E402
from mock_server import serve, STATE             # noqa: E402

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    sync_playwright = None

PORT = 8931
URL = f"http://127.0.0.1:{PORT}/"


def chrome():
    hits = sorted(glob.glob(os.path.expanduser(
        "~/.cache/ms-playwright/chromium-*/chrome-linux64/chrome")))
    return hits[-1] if hits else None


@unittest.skipIf(sync_playwright is None or chrome() is None, "playwright/chromium absent")
class OtaPageTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.srv = serve(PORT)
        cls.tmp = tempfile.TemporaryDirectory()
        cls.files = {}
        cls.stale = images.good(version="v1.17.1-stale00", sha="stale00")
        import hashlib
        cls.md5_stale = hashlib.md5(cls.stale).hexdigest()
        cls.md5_reject = hashlib.md5(images.full_flash()).hexdigest()
        cls.md5_good = hashlib.md5(images.good()).hexdigest()
        for name, blob in [("good", images.good()), ("full_flash", images.full_flash()),
                           ("stale", cls.stale),
                           ("wrong_chip", images.wrong_chip()),
                           ("wrong_role", images.wrong_role()),
                           ("no_ota", images.no_hotspot_ota()), ("tiny", images.tiny())]:
            p = pathlib.Path(cls.tmp.name) / f"{name}.bin"
            p.write_bytes(blob)
            cls.files[name] = str(p)
        cls.pw = sync_playwright().start()
        cls.browser = cls.pw.chromium.launch(executable_path=chrome(), headless=True)

    @classmethod
    def tearDownClass(cls):
        cls.browser.close()
        cls.pw.stop()
        cls.srv.shutdown()
        cls.tmp.cleanup()

    def page(self):
        pg = self.browser.new_page()
        pg.set_default_timeout(15000)
        pg.goto(URL)
        pg.wait_for_function("() => document.querySelector('#identity').textContent !== 'Detecting identity…'")
        return pg

    def state(self, pg):
        return pg.evaluate("""() => ({
            verdict: document.querySelector('#nt').textContent,
            cls: document.querySelector('#n').className,
            disabled: document.querySelector('#upload').disabled,
            override: !document.querySelector('#ow').hidden,
        })""")

    def pick(self, pg, name):
        pg.set_input_files("#pick", self.files[name])

    def drop(self, pg, name):
        blob = pathlib.Path(self.files[name]).read_bytes()
        pg.evaluate("""async ({bytes, name}) => {
            const f = new File([new Uint8Array(bytes)], name, {type: 'application/octet-stream'});
            const dt = new DataTransfer();
            dt.items.add(f);
            document.querySelector('#drop').dispatchEvent(
                new DragEvent('drop', {dataTransfer: dt, bubbles: true, cancelable: true}));
        }""", {"bytes": list(blob), "name": f"{name}.bin"})

    def settled(self, pg):
        pg.wait_for_function("() => document.querySelector('#n').className.includes('show')")
        pg.wait_for_function("() => !document.querySelector('#hash').textContent.includes('calculating')")

    # 1 -- the defect that let a dropped file skip validation entirely
    def test_picker_and_drop_validate_identically(self):
        for name in ("good", "full_flash", "wrong_chip", "wrong_role", "no_ota"):
            with self.subTest(image=name):
                p1 = self.page(); self.pick(p1, name); self.settled(p1)
                by_picker = self.state(p1); p1.close()

                p2 = self.page(); self.drop(p2, name); self.settled(p2)
                by_drop = self.state(p2); p2.close()

                self.assertEqual(by_picker, by_drop,
                                 f"{name}: picker and drop disagree")

    # 2 -- Install must wait for identity, header and MD5 together
    def test_install_never_enables_before_everything_is_ready(self):
        pg = self.page()
        pg.evaluate("""() => {
            window.__seen = [];
            const u = document.querySelector('#upload');
            new MutationObserver(() => window.__seen.push({
                disabled: u.disabled,
                md5: !document.querySelector('#hash').textContent.includes('calculating'),
                verdict: document.querySelector('#n').className.includes('show'),
            })).observe(u, {attributes: true, attributeFilter: ['disabled']});
        }""")
        self.pick(pg, "good")
        self.settled(pg)
        pg.wait_for_function("() => !document.querySelector('#upload').disabled")
        bad = pg.evaluate("() => window.__seen.filter(s => !s.disabled && !(s.md5 && s.verdict))")
        self.assertEqual(bad, [], f"Install enabled before ready: {bad}")
        pg.close()

    def _race(self, pg, first, second):
        """Force the window the generation guard exists to close.

        The page's md5 is too fast to overlap on its own, so the first file's read is
        delayed in-page: its hash finishes last, and without the guard lands on the
        selection that replaced it.
        """
        a = list(pathlib.Path(self.files[first]).read_bytes())
        b = list(pathlib.Path(self.files[second]).read_bytes())
        pg.evaluate("""({a, b, an, bn}) => {
            // Asynchronous delay: a busy-wait blocks the single JS thread and lets the
            // superseded hash finish first, which is the opposite of the race tested.
            const real = File.prototype.arrayBuffer;
            let first = true;
            File.prototype.arrayBuffer = function () {
                const p = real.call(this);
                if (!first) return p;
                first = false;
                return p.then(buf => new Promise(r => setTimeout(() => r(buf), 500)));
            };
            const mk = (bytes, name) => new File([new Uint8Array(bytes)], name,
                                                 {type: 'application/octet-stream'});
            S(mk(a, an));
            S(mk(b, bn));
        }""", {"a": a, "b": b, "an": f"{first}.bin", "bn": f"{second}.bin"})

    # 3 -- a stale hash landing on the new selection would upload one file's bytes
    # under another file's checksum.
    def test_stale_selection_is_ignored(self):
        pg = self.page()
        self._race(pg, "stale", "full_flash")
        self.settled(pg)
        pg.wait_for_timeout(1200)
        st = self.state(pg)
        self.assertIn("full-flash", st["verdict"], f"verdict tracked the stale file: {st}")
        self.assertTrue(st["disabled"], "Install enabled for a rejected image")

        shown = pg.evaluate("() => document.querySelector('#hash').textContent")
        self.assertNotIn(self.md5_stale, shown, "a superseded file's MD5 reached the new selection")
        pg.close()

    # The same race with a valid replacement: the stale hash must not overwrite it.
    def test_stale_hash_does_not_overwrite_current_selection(self):
        pg = self.page()
        self._race(pg, "stale", "good")
        self.settled(pg)
        pg.wait_for_timeout(1200)
        shown = pg.evaluate("() => document.querySelector('#hash').textContent")
        self.assertNotIn(self.md5_stale, shown, "stale MD5 overwrote the current selection")
        self.assertIn(self.md5_good, shown, f"wrong MD5 shown: {shown}")
        held = pg.evaluate("() => (s && s.m) || ''")
        self.assertEqual(held, self.md5_good, "the selection holds a hash from the wrong file")
        pg.close()

    # 4 -- the MutationObserver used to re-disable Install after a failure
    def test_retry_after_http_failure(self):
        self._retry_case("http500", "Update failed")

    def test_retry_after_network_failure(self):
        self._retry_case("drop", "Connection lost")

    def _retry_case(self, mode, expect):
        STATE["upload"] = mode
        pg = self.page()
        self.pick(pg, "good")
        self.settled(pg)
        pg.wait_for_function("() => !document.querySelector('#upload').disabled")
        pg.click("#upload")
        pg.wait_for_function("() => document.querySelector('#msg').className.includes('bad')")
        msg = pg.evaluate("() => document.querySelector('#msg').textContent")
        self.assertIn(expect, msg)
        pg.wait_for_function("() => !document.querySelector('#upload').disabled",
                             timeout=10000)
        label = pg.evaluate("() => document.querySelector('#upload').textContent")
        self.assertIn("Retry", label)
        STATE["upload"] = "ok"
        pg.close()

    # A malformed file must be rejected, not throw
    def test_tiny_file_is_rejected_cleanly(self):
        errors = []
        pg = self.page()
        pg.on("pageerror", lambda e: errors.append(str(e)))
        self.pick(pg, "tiny")
        pg.wait_for_function("() => document.querySelector('#n').className.includes('show')")
        st = self.state(pg)
        self.assertTrue(st["disabled"], "Install enabled for a malformed file")
        self.assertEqual(errors, [], f"uncaught error: {errors}")
        pg.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
