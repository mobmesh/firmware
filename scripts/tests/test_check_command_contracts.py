"""Command contract check against a two-commit upstream built in a temp repo."""
import importlib.util
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("check_command_contracts", REPO_ROOT / "scripts/check_command_contracts.py")
contracts = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(contracts)

BASE_CLI = """
void CommonCLI::handleCommand(char* command, char* reply) {
  if (memcmp(command, "reboot", 6) == 0) {}
  if (memcmp(command, "tempradio ", 10) == 0) { sprintf(reply, "OK - temp params for %d mins", 1); }
  if (memcmp(config, "af ", 3) == 0) {}
  if (memcmp(config, "radio.fem.txgain ", 17) == 0) {}
  if (memcmp(config, "radio ", 6) == 0) { strcpy(reply, "OK - reboot to apply"); }
  sprintf(reply, "> %d", 1);
  if (memcmp(config, "mod.only ", 9) == 0) {}
}
"""
HEAD_CLI = """
void CommonCLI::handleCommand(char* command, char* reply) {
  if (memcmp(command, "tempradio ", 10) == 0) { sprintf(reply, "OK - temp params for %d mins", 1); }
  sprintf(reply, "> %d", 1);
  // the old reboot verb lives on as a doc string: "reboot now"
}
"""
HEAD_PREFS = """
void CommonRadioPrefs::handle(char* command, char* reply) {
  if (memcmp(command, "set af ", 7) == 0) {}
  if (memcmp(command, "set radio ", 10) == 0) { strcpy(reply, "OK - reboot to apply"); }
}
"""
HEAD_OTHER_BOARD = """
bool OtherBoard::handleCommand(char* command, char* reply) {
  if (memcmp(command, "set radio.fem.txgain ", 21) == 0) {}
}
"""
MOD_SOURCE = """
// "tempradio" in a comment is not a use
static const char* url = "http://example/x"; static const char* verb = "reboot";
static void a() { nativeSet("set af %.9g", 1.0); radioCommand("set radio", v); }
static const char* const KEYS[] = { "radio.fem.txgain", "mod.owned", nullptr };
"""


def git(tree, *args):
    subprocess.run(["git", "-C", str(tree), *args], check=True, capture_output=True)


class CommandContractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.tree = root / "upstream"
        (self.tree / "src/helpers").mkdir(parents=True)
        (self.tree / "src/helpers/ESP32Board.h").write_text("tv.tv_sec = 1715770351;\n")
        (self.tree / "src/helpers/CommonCLI.cpp").write_text(BASE_CLI)
        git(self.tree, "init", "-q")
        git(self.tree, "add", "-A")
        git(self.tree, "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-qm", "base")
        git(self.tree, "tag", "base")
        (self.tree / "src/helpers/CommonCLI.cpp").write_text(HEAD_CLI)
        (self.tree / "src/helpers/CommonRadioPrefs.cpp").write_text(HEAD_PREFS)
        (self.tree / "variants/other").mkdir(parents=True)
        (self.tree / "variants/other/OtherBoard.cpp").write_text(HEAD_OTHER_BOARD)
        git(self.tree, "add", "-A")
        git(self.tree, "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-qm", "head")
        self.mods = root / "mods"
        (self.mods / "demo/files/src").mkdir(parents=True)
        (self.mods / "demo/files/src/Demo.cpp").write_text(MOD_SOURCE)

    def tearDown(self):
        self.tmp.cleanup()

    def run_check(self, head="HEAD"):
        return contracts.check(self.tree, "base", head, ["ours"], self.mods)

    def test_release_against_itself_is_clean(self):
        problems, moved, ok, replies = self.run_check("base")
        self.assertEqual((problems, moved, replies), ([], [], []))
        self.assertEqual(ok, 4)

    def test_url_literal_does_not_hide_later_literals(self):
        self.assertIn("reboot", contracts.mod_candidates(self.mods))

    def test_comment_mention_is_not_a_use(self):
        self.assertNotIn("tempradio", contracts.mod_candidates(self.mods))

    def test_mod_only_verbs_are_not_contracts(self):
        problems, moved, ok, _ = self.run_check()
        keys = {p[0] for p in problems} | {m[0] for m in moved}
        self.assertNotIn("mod.owned", keys)
        self.assertNotIn("mod.only", keys)

    def test_removed_command_is_reported_with_hints(self):
        problems, *_ = self.run_check()
        reboot = [p for p in problems if p[0] == "reboot"]
        self.assertEqual(len(reboot), 1)
        self.assertIn("no longer parsed", reboot[0][2])
        self.assertIn("src/helpers/CommonCLI.cpp:5", reboot[0][3])

    def test_command_only_on_other_boards_is_reported(self):
        problems, *_ = self.run_check()
        fem = [p for p in problems if p[0] == "radio.fem.txgain"]
        self.assertEqual(len(fem), 1)
        self.assertIn("boards we do not build", fem[0][2])
        self.assertEqual(fem[0][3], ["variants/other/OtherBoard.cpp:3"])

    def test_verb_prefixed_move_counts_as_moved_not_missing(self):
        problems, moved, _, _ = self.run_check()
        self.assertNotIn("af", {p[0] for p in problems})
        self.assertIn(("af", "demo", ["src/helpers/CommonCLI.cpp"], ["src/helpers/CommonRadioPrefs.cpp"]), moved)

    def test_board_we_build_satisfies_the_contract(self):
        problems, *_ = contracts.check(self.tree, "base", "HEAD", ["other"], self.mods)
        self.assertNotIn("radio.fem.txgain", {p[0] for p in problems})

    def test_lost_reply_contract_is_reported(self):
        (self.tree / "src/helpers/ESP32Board.h").write_text("tv.tv_sec = 0;\n")
        git(self.tree, "add", "-A")
        git(self.tree, "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-qm", "epoch")
        *_, replies = self.run_check()
        self.assertEqual([r[0] for r in replies], ["default clock epoch 1715770351"])


if __name__ == "__main__":
    unittest.main()
