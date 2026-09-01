import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import qemu_boot_check as qbc


class WaitForCliTestCase(unittest.TestCase):
    def test_sends_one_probe_for_the_remaining_timeout(self):
        console = mock.Mock()
        console.ask.return_value = "v1.17.1"

        with mock.patch.object(qbc.time, "time", return_value=10):
            reply = qbc.wait_for_cli(console, 40)

        self.assertEqual(reply, "v1.17.1")
        console.ask.assert_called_once_with("ver", timeout=30, quiet=True)


if __name__ == "__main__":
    unittest.main()
