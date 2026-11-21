"""No two mods may claim the same command, or a command that is a prefix of another mod's.

Mod CLI handlers run highest priority first and the first to answer wins, so an overlap
silently swallows the other mod's command on any board carrying both. Syntaxes come from
each mod's cli-additions.md code blocks, the same source the console catalog reads.
"""

import re
import unittest
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MODS = REPO_ROOT / "mods"

# Deliberate overrides: the first mod answers before the second, which handles the board without it.
OVERRIDES = {
    "ver": ("hotspot-ota", "core"),
    "poweroff": ("power-guard", "core"),
    "shutdown": ("power-guard", "core"),
}

COMMAND_RE = re.compile(r"^[a-z][a-z0-9._-]*$")


def command_head(line):
    """Literal words before the first parameter; a word with a parameter inside ends as a partial prefix."""
    words = line.split()
    if not words or not COMMAND_RE.match(words[0]):
        return None
    head = []
    for word in words:
        if word[0] in "<[{" or "|" in word:
            break
        cut = min((word.index(c) for c in "<[{" if c in word), default=None)
        if cut is not None:
            head.append(("partial", word[:cut]))
            break
        head.append(("word", word))
    return tuple(head)


def covers(a, b):
    """Whether a command starting with head `a` would also match head `b`."""
    if len(a) > len(b):
        return False
    for (kind_a, word_a), (_, word_b) in zip(a, b):
        if kind_a == "partial":
            return word_b.startswith(word_a)
        if word_a != word_b:
            return False
    return True


def text(head):
    return " ".join(word + ("…" if kind == "partial" else "") for kind, word in head)


def mod_commands():
    found = {}
    for doc in sorted(MODS.glob("*/docs/cli-additions.md")):
        heads = set()
        for block in re.findall(r"```(?:text)?\n(.*?)```", doc.read_text(), re.S):
            for line in block.splitlines():
                head = command_head(line.strip())
                if head:
                    heads.add(head)
        found[doc.parent.parent.name] = heads
    return found


def cli_priority(mod):
    data = yaml.safe_load((MODS / mod / "mod.yaml").read_text())
    return data["integration"]["hooks"]["cli"]["priority"]


class CliClashTestCase(unittest.TestCase):
    def test_no_mod_claims_another_mods_command(self):
        commands = mod_commands()
        clashes = []
        mods = sorted(commands)
        for i, first in enumerate(mods):
            for second in mods[i + 1:]:
                for a in commands[first]:
                    for b in commands[second]:
                        if not (covers(a, b) or covers(b, a)):
                            continue
                        name = text(a if len(a) <= len(b) else b).split()[0]
                        if name in OVERRIDES and set(OVERRIDES[name]) == {first, second}:
                            continue
                        clashes.append(f"{first} `{text(a)}` overlaps {second} `{text(b)}`")
        self.assertEqual(clashes, [], "\n".join(clashes))

    def test_overrides_answer_first(self):
        commands = mod_commands()
        for name, (winner, fallback) in OVERRIDES.items():
            self.assertTrue(any(h and h[0][1] == name for h in commands[winner]), f"{winner} no longer documents {name}")
            self.assertTrue(any(h and h[0][1] == name for h in commands[fallback]), f"{fallback} no longer documents {name}")
            self.assertGreater(cli_priority(winner), cli_priority(fallback), f"{winner} must answer `{name}` before {fallback}")

    def test_parser_reads_heads_and_prefixes(self):
        self.assertEqual(text(command_head("powersaving safe [on|off]")), "powersaving safe")
        self.assertEqual(text(command_head("get sync.<dataset>.publish.interval")), "get sync.…")
        self.assertIsNone(command_head("<upstream-version> (<date>)"))
        self.assertIsNone(command_head("Reply from c5fb: time=1334ms"))
        self.assertTrue(covers(command_head("powersaving"), command_head("powersaving safe")))
        self.assertFalse(covers(command_head("powersaving safe"), command_head("powersaving safe.mv <mv>")))
        self.assertTrue(covers(command_head("get sync.<x>"), command_head("get sync.channel")))


if __name__ == "__main__":
    unittest.main()
