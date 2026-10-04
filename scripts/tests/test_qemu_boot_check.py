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


class SerialArgsTestCase(unittest.TestCase):
    def test_usb_console_is_serial_two(self):
        args = qbc.serial_args("usb", Path("u.log"))
        self.assertEqual(args[1], "file:u.log")
        self.assertIn("tcp:", args[5])

    def test_otg_console_is_serial_three(self):
        args = qbc.serial_args("otg", Path("u.log"))
        self.assertEqual(args[1], "file:u.log")
        self.assertIn("tcp:", args[7])

    def test_uart0_console_is_serial_zero_and_logged(self):
        args = qbc.serial_args("uart0", Path("u.log"))
        self.assertTrue(args[1].startswith("socket,id=uart0,"))
        self.assertIn("logfile=u.log", args[1])
        self.assertEqual(args[3], "chardev:uart0")


if __name__ == "__main__":
    unittest.main()
