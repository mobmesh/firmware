import argparse
import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT_PATH = Path(__file__).resolve().parent.parent / "generate-board-config.py"
spec = importlib.util.spec_from_file_location("generate_board_config_composition", SCRIPT_PATH)
gbc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gbc)


class RealCompositionTestCase(unittest.TestCase):
    def compose(self, mods):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        upstream = Path(temp.name)
        gbc.cmd_compose_mods(argparse.Namespace(upstream=str(upstream), mods=mods))
        return (
            (upstream / "src/helpers/ModHooks.cpp").read_text(),
            (upstream / "src/helpers/esp32/CommonCliMods.cpp").read_text(),
        )

    def test_shipped_target_order_matches_existing_behavior(self):
        hooks, cli = self.compose("shim,hotspot-ota,timing-safety,power-guard")
        self.assertLess(hooks.index("hotspotOtaLoop();"), hooks.index("powerGuardLoop();"))
        # hotspot-ota must reach the CLI first: power-guard consumes poweroff/shutdown
        # unconditionally, so the OTA interlock would never see them from behind it.
        self.assertLess(cli.index("hotspotOtaHandleCli"), cli.index("powerGuardHandleCli"))
        self.assertIn("return hotspotOtaRadioInit(build_id);", hooks)
        self.assertIn("powerGuardBeforeRadioInit();", hooks)

    def test_selecting_a_mod_only_adds_its_calls(self):
        base_hooks, base_cli = self.compose("shim,timing-safety")
        ota_hooks, ota_cli = self.compose("shim,hotspot-ota,timing-safety")
        power_hooks, power_cli = self.compose("shim,timing-safety,power-guard")

        self.assertNotIn("hotspotOta", base_hooks + base_cli)
        self.assertNotIn("powerGuard", base_hooks + base_cli)
        self.assertIn("hotspotOta", ota_hooks + ota_cli)
        self.assertNotIn("powerGuard", ota_hooks + ota_cli)
        self.assertIn("powerGuard", power_hooks + power_cli)
        self.assertNotIn("hotspotOta", power_hooks + power_cli)
        self.assertIn("return modBoardRadioInit();", power_hooks)

    def test_generation_is_byte_deterministic(self):
        first = self.compose("shim,hotspot-ota,timing-safety,power-guard")
        second = self.compose("shim,hotspot-ota,timing-safety,power-guard")
        self.assertEqual(first, second)

    def test_feature_patches_no_longer_exist(self):
        root = SCRIPT_PATH.parent.parent
        self.assertEqual(list((root / "mods/hotspot-ota/patches").glob("*")), [])
        self.assertEqual(list((root / "mods/power-guard/patches").glob("*")), [])

    def test_aggregate_sources_are_generated_only(self):
        root = SCRIPT_PATH.parent.parent
        outputs = gbc.composition_outputs(["shim"])
        for output in outputs.values():
            self.assertFalse((root / "mods/shim/files" / output).exists())


class InvalidCompositionTestCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.original_root = gbc.REPO_ROOT
        gbc.REPO_ROOT = self.root
        self.addCleanup(self.restore)
        self.write_manifest("shim", "name: shim\ncomposition:\n  outputs:\n    hooks: src/helpers/ModHooks.cpp\n    cli: src/helpers/esp32/CommonCliMods.cpp\n")

    def restore(self):
        gbc.REPO_ROOT = self.original_root
        self.temp.cleanup()

    def write_manifest(self, mod, text):
        path = self.root / "mods" / mod / "mod.yaml"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def write_header(self, mod, text):
        path = self.root / "mods" / mod / "files/src/helpers/TestIntegration.h"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def integration(self, symbol="testRadioInit", phase="radio_init_policy"):
        return (
            "name: feature\n"
            "integration:\n"
            "  header: helpers/TestIntegration.h\n"
            "  hooks:\n"
            f"    {phase}: {symbol}\n"
        )

    def test_two_radio_policies_are_rejected(self):
        self.write_header("first", "bool firstRadio(const char*);\n")
        self.write_manifest("first", self.integration("firstRadio").replace("name: feature", "name: first"))
        self.write_header("second", "bool secondRadio(const char*);\n")
        self.write_manifest("second", self.integration("secondRadio").replace("name: feature", "name: second"))
        with self.assertRaisesRegex(ValueError, "radio_init_policy is exclusive"):
            gbc.load_integrations(["first", "second"])

    def test_unknown_phase_is_rejected(self):
        self.write_header("feature", "void testHook();\n")
        self.write_manifest("feature", self.integration("testHook", "unknown_phase"))
        with self.assertRaisesRegex(ValueError, "unknown integration phase"):
            gbc.load_integrations(["feature"])

    def test_symbol_must_be_declared_in_header(self):
        self.write_header("feature", "bool anotherSymbol();\n")
        self.write_manifest("feature", self.integration())
        with self.assertRaisesRegex(ValueError, "is not declared"):
            gbc.load_integrations(["feature"])

    def mesh_integration(self, block, name="feature"):
        return (
            f"name: {name}\n"
            "integration:\n"
            "  header: helpers/TestIntegration.h\n"
            "  hooks:\n"
            "    mesh:\n" + block
        )

    def test_mesh_consumer_is_accepted_and_chains(self):
        self.write_header("feature", "bool takePacket(mesh::Packet*);\n")
        self.write_manifest("feature", self.mesh_integration(
            "      consumers:\n        onRecvPacket: takePacket\n"))
        rendered = gbc.render_mod_mesh(gbc.load_integrations(["feature"]))
        self.assertIn("onRecvPacket", rendered)
        self.assertIn("if (takePacket(packet)) return mesh::ACTION_RELEASE;", rendered)
        # A consumer that declines must fall through to the class it was interposed above.
        self.assertIn("return Base::onRecvPacket(packet);", rendered)

    def test_mesh_observer_always_calls_through(self):
        self.write_header("feature", "void sawAdvert(mesh::Packet*, const mesh::Identity&, uint32_t, const uint8_t*, size_t);\n")
        self.write_manifest("feature", self.mesh_integration(
            "      observers:\n        onAdvertRecv: sawAdvert\n"))
        rendered = gbc.render_mod_mesh(gbc.load_integrations(["feature"]))
        self.assertIn("sawAdvert(packet, id, timestamp, app_data, app_data_len);", rendered)
        self.assertIn("Base::onAdvertRecv(", rendered)

    def test_value_producing_virtual_is_rejected(self):
        # Two mods answering getRetransmitDelay cannot both be right, so it is refused.
        self.write_header("feature", "uint32_t pickDelay(const mesh::Packet*);\n")
        self.write_manifest("feature", self.mesh_integration(
            "      consumers:\n        getRetransmitDelay: pickDelay\n"))
        with self.assertRaisesRegex(Exception, "not a consumer ModMesh knows how to override"):
            gbc.load_integrations(["feature"])

    def test_mesh_symbol_must_be_declared_in_header(self):
        self.write_header("feature", "bool somethingElse(mesh::Packet*);\n")
        self.write_manifest("feature", self.mesh_integration(
            "      consumers:\n        onRecvPacket: takePacket\n"))
        with self.assertRaisesRegex(ValueError, "is not declared"):
            gbc.load_integrations(["feature"])

    def test_two_mods_may_not_share_a_mesh_symbol(self):
        for mod in ("first", "second"):
            self.write_header(mod, "bool takePacket(mesh::Packet*);\n")
            self.write_manifest(mod, self.mesh_integration(
                "      consumers:\n        onRecvPacket: takePacket\n", name=mod))
        with self.assertRaisesRegex(ValueError, "declared by both"):
            gbc.load_integrations(["first", "second"])

    def test_mesh_output_must_be_a_header(self):
        self.write_manifest("shim", "name: shim\ncomposition:\n  outputs:\n"
                            "    hooks: src/helpers/ModHooks.cpp\n"
                            "    cli: src/helpers/esp32/CommonCliMods.cpp\n"
                            "    mesh: src/helpers/ModMesh.cpp\n")
        with self.assertRaisesRegex(ValueError, "invalid mesh composition output"):
            gbc.composition_outputs(["shim"])

    def test_existing_output_is_not_overwritten(self):
        upstream = self.root / "upstream"
        output = upstream / "src/helpers/ModHooks.cpp"
        output.parent.mkdir(parents=True)
        output.write_text("upstream\n")
        with self.assertRaises(FileExistsError):
            gbc.cmd_compose_mods(argparse.Namespace(upstream=str(upstream), mods="shim"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
