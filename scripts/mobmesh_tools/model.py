from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Mapping

import yaml


class ProjectModelError(ValueError):
    pass


class Capability(str, Enum):
    WIFI_STATION = "wifi_station"
    DUAL_OTA = "dual_ota"
    BATTERY_MEASUREMENT = "battery_measurement"
    EXTERNAL_POWER_CONTROL = "external_power_control"
    FEM_LNA_CONTROL = "fem_lna_control"
    DEEP_SLEEP_RAIL_SHUTDOWN = "deep_sleep_rail_shutdown"


class CapabilityState(str, Enum):
    ABSENT = "absent"
    AVAILABLE = "available"
    UNVERIFIED = "unverified"

    @property
    def satisfies_requirement(self) -> bool:
        return self is self.AVAILABLE


class IntegrationPhase(str, Enum):
    BEFORE_RADIO_INIT = "before_radio_init"
    RADIO_INIT_POLICY = "radio_init_policy"
    LOOP = "loop"
    WANTS_POWER_SAVING = "wants_power_saving"
    CLI = "cli"


Scalar = str | int | float | bool


def _load_yaml(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise ProjectModelError(f"{path}: file does not exist")
    data = yaml.safe_load(path.read_text()) or {}
    if not isinstance(data, dict):
        raise ProjectModelError(f"{path}: expected a mapping at the document root")
    return data


def _keys(path: Path, field_name: str, data: Mapping[str, Any], allowed: set[str]) -> None:
    unknown = set(data) - allowed
    if unknown:
        raise ProjectModelError(f"{path}:{field_name}: unknown field(s): {sorted(unknown)}")


def _mapping(path: Path, field_name: str, value: Any) -> dict[str, Any]:
    if value is None:
        return {}
    if not isinstance(value, dict):
        raise ProjectModelError(f"{path}:{field_name}: expected a mapping")
    return value


def _scalars(path: Path, field_name: str, value: Any) -> dict[str, Scalar]:
    data = _mapping(path, field_name, value)
    for name, item in data.items():
        if not isinstance(name, str) or not name:
            raise ProjectModelError(f"{path}:{field_name}: keys must be nonempty strings")
        if not isinstance(item, (str, int, float, bool)):
            raise ProjectModelError(f"{path}:{field_name}.{name}: expected a scalar value")
    return data


def _string(path: Path, field_name: str, value: Any) -> str:
    if not isinstance(value, str) or not value:
        raise ProjectModelError(f"{path}:{field_name}: expected a nonempty string")
    return value


def _boolean(path: Path, field_name: str, value: Any) -> bool:
    if not isinstance(value, bool):
        raise ProjectModelError(f"{path}:{field_name}: expected true or false")
    return value


def _strings(path: Path, field_name: str, value: Any) -> tuple[str, ...]:
    if value is None:
        return ()
    if not isinstance(value, list) or any(not isinstance(item, str) or not item for item in value):
        raise ProjectModelError(f"{path}:{field_name}: expected a list of nonempty strings")
    if len(value) != len(set(value)):
        raise ProjectModelError(f"{path}:{field_name}: duplicate values are not allowed")
    return tuple(value)


def _capability(path: Path, field_name: str, value: Any) -> Capability:
    try:
        return Capability(value)
    except (TypeError, ValueError) as exc:
        raise ProjectModelError(f"{path}:{field_name}: unknown capability '{value}'") from exc


def _capability_state(path: Path, field_name: str, value: Any) -> CapabilityState:
    if value is True:
        return CapabilityState.AVAILABLE
    if value is False:
        return CapabilityState.ABSENT
    try:
        return CapabilityState(value)
    except (TypeError, ValueError) as exc:
        raise ProjectModelError(
            f"{path}:{field_name}: expected true, false, or unverified"
        ) from exc


@dataclass(frozen=True)
class CliIntegration:
    handler: str
    priority: int


@dataclass(frozen=True)
class IntegrationDefinition:
    header: str
    hooks: Mapping[IntegrationPhase, str | CliIntegration]


@dataclass(frozen=True)
class CompositionOutputs:
    hooks: str
    cli: str


@dataclass(frozen=True)
class PatchDefinition:
    mod: str
    patch_id: str
    title: str
    patch_path: Path
    sidecar_path: Path
    requires: tuple[str, ...]
    upstream_prs: tuple[int, ...]
    env_flag: str | None
    build_src_filter: tuple[str, ...]

    @property
    def qualified_id(self) -> str:
        return f"{self.mod}/{self.patch_id}"


def _ota_web_page(path: Path, value: Any) -> str | None:
    """A mod-relative path to the HTML this mod serves in place of upstream's OTA page."""
    if value is None:
        return None
    if not isinstance(value, str) or not value or value.startswith("/") or ".." in Path(value).parts:
        raise ProjectModelError(f"{path}:ota_web_page: expected a mod-relative file path")
    return value


@dataclass(frozen=True)
class ModDefinition:
    name: str
    source_path: Path
    env_flags: tuple[str, ...]
    bit: int | None
    image_marker: str | None
    required_capabilities: tuple[Capability, ...]
    optional_capabilities: tuple[Capability, ...]
    integration: IntegrationDefinition | None
    composition: CompositionOutputs | None
    ota_web_page: str | None
    patches: tuple[PatchDefinition, ...]


@dataclass(frozen=True)
class QemuProfile:
    enabled: bool
    machine: str | None
    binary: str | None
    mcu: str | None
    mem: str
    globals: Mapping[str, Scalar]


@dataclass(frozen=True)
class FlasherProfile:
    label: str
    connect_note: str
    post_flash_note: str
    post_flash_commands: Mapping[str, tuple[str, ...]]


@dataclass(frozen=True)
class PartitionLayout:
    otadata_offset: int
    app0_offset: int
    app0_size: int
    app1_offset: int


@dataclass(frozen=True)
class BoardProfile:
    board_id: str
    source_path: Path
    capabilities: Mapping[Capability, CapabilityState]
    build_values: Mapping[str, Scalar]
    build_flags_append: tuple[str, ...]
    partitions_override: str | None
    qemu: QemuProfile
    flasher: FlasherProfile

    def capability(self, capability: Capability) -> CapabilityState:
        return self.capabilities.get(capability, CapabilityState.ABSENT)


@dataclass(frozen=True)
class FirmwareRole:
    role_id: str
    asset_role_abbrev: str
    upstream_tag_prefix: str
    release_title: str
    make_latest: bool


@dataclass(frozen=True)
class TargetDefinition:
    board: str
    role: str
    build_env: str
    vendor_flasher_assets: bool
    mods: tuple[str, ...]
    build_flags_append: tuple[str, ...]
    qemu_boot_check: bool


@dataclass(frozen=True)
class ResolvedTarget:
    target_id: str
    board_id: str
    role: str
    build_env: str
    upstream_tag_prefix: str
    release_title: str
    asset_basename: str
    vendor_flasher_assets: bool
    make_latest: bool
    mods: tuple[str, ...]
    build_flags_append: tuple[str, ...]
    qemu_boot_check: bool
    capabilities: Mapping[str, str]

    def as_dict(self) -> dict[str, Any]:
        return {
            "id": self.target_id,
            "board_id": self.board_id,
            "role": self.role,
            "build_env": self.build_env,
            "upstream_tag_prefix": self.upstream_tag_prefix,
            "release_title": self.release_title,
            "asset_basename": self.asset_basename,
            "vendor_flasher_assets": self.vendor_flasher_assets,
            "make_latest": self.make_latest,
            "mods": list(self.mods),
            "build_flags_append": list(self.build_flags_append),
            "qemu_boot_check": self.qemu_boot_check,
            "capabilities": dict(self.capabilities),
        }


@dataclass(frozen=True)
class BuildPlan:
    core_mods: tuple[str, ...]
    targets: tuple[ResolvedTarget, ...]

    def as_dict(self) -> dict[str, Any]:
        return {
            "core_mods": list(self.core_mods),
            "targets": [target.as_dict() for target in self.targets],
        }

    def to_json(self) -> str:
        return json.dumps(self.as_dict(), separators=(",", ":"))

    def matrix_json(self) -> str:
        return json.dumps({"include": [target.as_dict() for target in self.targets]})


@dataclass
class ProjectModel:
    root: Path
    core_mods: tuple[str, ...]
    roles: Mapping[str, FirmwareRole]
    targets: tuple[TargetDefinition, ...]
    boards: Mapping[str, BoardProfile]
    mods: Mapping[str, ModDefinition]
    build_plan: BuildPlan = field(init=False)

    @classmethod
    def load(cls, root: Path) -> "ProjectModel":
        root = root.resolve()
        config_path = root / "build-targets.yaml"
        data = _load_yaml(config_path)
        _keys(config_path, "root", data, {"core_mods", "roles", "targets"})

        core_mods = _strings(config_path, "core_mods", data.get("core_mods"))
        roles = cls._load_roles(config_path, data.get("roles"))
        targets = cls._load_targets(config_path, data.get("targets"))

        board_ids = {target.board for target in targets}
        boards = {board_id: cls._load_board(root, board_id) for board_id in sorted(board_ids)}
        mod_names = {path.parent.name for path in (root / "mods").glob("*/mod.yaml")}
        mod_names.update(core_mods)
        for target in targets:
            mod_names.update(target.mods)
        mods = {name: cls._load_mod(root, name) for name in sorted(mod_names)}

        model = cls(root, core_mods, roles, targets, boards, mods)
        model._validate()
        model.build_plan = model._resolve()
        return model

    @staticmethod
    def _load_roles(path: Path, value: Any) -> dict[str, FirmwareRole]:
        data = _mapping(path, "roles", value)
        roles = {}
        for role_id, raw in data.items():
            _string(path, "roles", role_id)
            item = _mapping(path, f"roles.{role_id}", raw)
            _keys(path, f"roles.{role_id}", item, {
                "asset_role_abbrev", "upstream_tag_prefix", "release_title", "make_latest"
            })
            roles[role_id] = FirmwareRole(
                role_id=role_id,
                asset_role_abbrev=_string(path, f"roles.{role_id}.asset_role_abbrev", item.get("asset_role_abbrev")),
                upstream_tag_prefix=_string(path, f"roles.{role_id}.upstream_tag_prefix", item.get("upstream_tag_prefix")),
                release_title=_string(path, f"roles.{role_id}.release_title", item.get("release_title")),
                make_latest=_boolean(path, f"roles.{role_id}.make_latest", item.get("make_latest")),
            )
        return roles

    @staticmethod
    def _load_targets(path: Path, value: Any) -> tuple[TargetDefinition, ...]:
        if not isinstance(value, list) or not value:
            raise ProjectModelError(f"{path}:targets: expected a nonempty list")
        targets = []
        allowed = {"board", "role", "build_env", "vendor_flasher_assets", "mods", "build_flags_append", "qemu_boot_check"}
        for index, raw in enumerate(value):
            field_name = f"targets[{index}]"
            item = _mapping(path, field_name, raw)
            _keys(path, field_name, item, allowed)
            targets.append(TargetDefinition(
                board=_string(path, f"{field_name}.board", item.get("board")),
                role=_string(path, f"{field_name}.role", item.get("role")),
                build_env=_string(path, f"{field_name}.build_env", item.get("build_env")),
                vendor_flasher_assets=_boolean(path, f"{field_name}.vendor_flasher_assets", item.get("vendor_flasher_assets")),
                mods=_strings(path, f"{field_name}.mods", item.get("mods")),
                build_flags_append=_strings(path, f"{field_name}.build_flags_append", item.get("build_flags_append")),
                qemu_boot_check=_boolean(path, f"{field_name}.qemu_boot_check", item.get("qemu_boot_check", True)),
            ))
        return tuple(targets)

    @staticmethod
    def _load_board(root: Path, board_id: str) -> BoardProfile:
        path = root / "variants" / board_id / "overrides.yaml"
        data = _load_yaml(path)
        _keys(path, "root", data, {"capabilities", "build_values", "build_flags_append", "partitions_override", "qemu", "flasher"})

        raw_capabilities = _mapping(path, "capabilities", data.get("capabilities"))
        capabilities = {
            _capability(path, f"capabilities.{name}", name): _capability_state(path, f"capabilities.{name}", state)
            for name, state in raw_capabilities.items()
        }
        build_values = _scalars(path, "build_values", data.get("build_values"))

        qemu_raw = _mapping(path, "qemu", data.get("qemu"))
        _keys(path, "qemu", qemu_raw, {"enabled", "machine", "binary", "mcu", "mem", "globals"})
        enabled = _boolean(path, "qemu.enabled", qemu_raw.get("enabled", False))
        qemu = QemuProfile(
            enabled=enabled,
            machine=_string(path, "qemu.machine", qemu_raw.get("machine")) if enabled else None,
            binary=_string(path, "qemu.binary", qemu_raw.get("binary")) if enabled else None,
            mcu=_string(path, "qemu.mcu", qemu_raw.get("mcu")) if enabled else None,
            mem=_string(path, "qemu.mem", qemu_raw.get("mem")) if enabled and qemu_raw.get("mem") != "" else "",
            globals=_scalars(path, "qemu.globals", qemu_raw.get("globals")),
        )

        flasher_raw = _mapping(path, "flasher", data.get("flasher"))
        _keys(path, "flasher", flasher_raw, {"label", "connect_note", "post_flash_note", "post_flash_commands"})
        command_raw = _mapping(path, "flasher.post_flash_commands", flasher_raw.get("post_flash_commands"))
        commands = {
            _string(path, "flasher.post_flash_commands", role):
                _strings(path, f"flasher.post_flash_commands.{role}", entries)
            for role, entries in command_raw.items()
        }
        flasher = FlasherProfile(
            label=_string(path, "flasher.label", flasher_raw.get("label")),
            connect_note=_string(path, "flasher.connect_note", flasher_raw.get("connect_note")),
            post_flash_note=_string(path, "flasher.post_flash_note", flasher_raw.get("post_flash_note")),
            post_flash_commands=commands,
        )
        partitions = data.get("partitions_override")
        if partitions is not None and (not isinstance(partitions, str) or not partitions):
            raise ProjectModelError(f"{path}:partitions_override: expected null or a filename")
        return BoardProfile(
            board_id=board_id,
            source_path=path,
            capabilities=capabilities,
            build_values=build_values,
            build_flags_append=_strings(path, "build_flags_append", data.get("build_flags_append")),
            partitions_override=partitions,
            qemu=qemu,
            flasher=flasher,
        )

    @classmethod
    def load_board(cls, root: Path, board_id: str) -> BoardProfile:
        return cls._load_board(root.resolve(), board_id)

    @staticmethod
    def _load_mod(root: Path, name: str) -> ModDefinition:
        path = root / "mods" / name / "mod.yaml"
        data = _load_yaml(path)
        _keys(path, "root", data, {"name", "env_flags", "bit", "image_marker", "requirements", "integration", "composition", "ota_web_page"})
        declared_name = _string(path, "name", data.get("name"))
        if declared_name != name:
            raise ProjectModelError(f"{path}:name: expected '{name}', found '{declared_name}'")

        requirements = _mapping(path, "requirements", data.get("requirements"))
        _keys(path, "requirements", requirements, {"required", "optional"})
        required = tuple(_capability(path, "requirements.required", item) for item in _strings(path, "requirements.required", requirements.get("required")))
        optional = tuple(_capability(path, "requirements.optional", item) for item in _strings(path, "requirements.optional", requirements.get("optional")))
        if set(required) & set(optional):
            raise ProjectModelError(f"{path}:requirements: a capability cannot be both required and optional")

        integration = ProjectModel._load_integration(path, data.get("integration"))
        composition = ProjectModel._load_composition(path, data.get("composition"))
        bit = data.get("bit")
        marker = data.get("image_marker")
        if bit is not None and (not isinstance(bit, int) or isinstance(bit, bool) or not 0 <= bit <= 31):
            raise ProjectModelError(f"{path}:bit: expected an integer from 0 through 31")
        if (bit is None) != (marker is None):
            raise ProjectModelError(f"{path}:bit,image_marker: both fields must be present together")
        if marker is not None:
            marker = _string(path, "image_marker", marker)
            try:
                marker.encode("ascii")
            except UnicodeEncodeError as exc:
                raise ProjectModelError(f"{path}:image_marker: expected ASCII text") from exc

        patches = ProjectModel._load_patches(root, name)
        return ModDefinition(
            name=name,
            source_path=path,
            env_flags=_strings(path, "env_flags", data.get("env_flags")),
            bit=bit,
            image_marker=marker,
            required_capabilities=required,
            optional_capabilities=optional,
            integration=integration,
            composition=composition,
            ota_web_page=_ota_web_page(path, data.get("ota_web_page")),
            patches=patches,
        )

    @classmethod
    def load_mod(cls, root: Path, name: str) -> ModDefinition:
        return cls._load_mod(root.resolve(), name)

    @staticmethod
    def _load_integration(path: Path, value: Any) -> IntegrationDefinition | None:
        if value is None:
            return None
        data = _mapping(path, "integration", value)
        _keys(path, "integration", data, {"header", "hooks"})
        hooks_raw = _mapping(path, "integration.hooks", data.get("hooks"))
        hooks: dict[IntegrationPhase, str | CliIntegration] = {}
        for phase, declaration in hooks_raw.items():
            try:
                integration_phase = IntegrationPhase(phase)
            except (TypeError, ValueError) as exc:
                raise ProjectModelError(
                    f"{path}:integration.hooks.{phase}: unknown integration phase"
                ) from exc
            if integration_phase is IntegrationPhase.CLI:
                cli = _mapping(path, "integration.hooks.cli", declaration)
                _keys(path, "integration.hooks.cli", cli, {"handler", "priority"})
                priority = cli.get("priority")
                if not isinstance(priority, int) or isinstance(priority, bool):
                    raise ProjectModelError(f"{path}:integration.hooks.cli.priority: expected an integer")
                hooks[integration_phase] = CliIntegration(
                    _string(path, "integration.hooks.cli.handler", cli.get("handler")), priority
                )
            else:
                hooks[integration_phase] = _string(path, f"integration.hooks.{phase}", declaration)
        return IntegrationDefinition(_string(path, "integration.header", data.get("header")), hooks)

    @staticmethod
    def _load_composition(path: Path, value: Any) -> CompositionOutputs | None:
        if value is None:
            return None
        data = _mapping(path, "composition", value)
        _keys(path, "composition", data, {"outputs"})
        outputs = _mapping(path, "composition.outputs", data.get("outputs"))
        _keys(path, "composition.outputs", outputs, {"hooks", "cli"})
        return CompositionOutputs(
            _string(path, "composition.outputs.hooks", outputs.get("hooks")),
            _string(path, "composition.outputs.cli", outputs.get("cli")),
        )

    @staticmethod
    def _load_patches(root: Path, mod: str) -> tuple[PatchDefinition, ...]:
        patch_dir = root / "mods" / mod / "patches"
        patch_files = {path.name.split("_", 1)[0]: path for path in patch_dir.glob("*.patch")}
        sidecars = {path.name.split(".", 1)[0]: path for path in patch_dir.glob("*.meta.yaml")}
        if set(patch_files) != set(sidecars):
            raise ProjectModelError(
                f"{patch_dir}: patch and sidecar ids differ: patches={sorted(patch_files)}, sidecars={sorted(sidecars)}"
            )
        patches = []
        for patch_id in sorted(patch_files):
            path = sidecars[patch_id]
            data = _load_yaml(path)
            _keys(path, "root", data, {"id", "title", "requires", "upstream_prs", "env_flag", "build_src_filter"})
            declared_id = str(data.get("id", ""))
            if declared_id != patch_id:
                raise ProjectModelError(f"{path}:id: expected '{patch_id}', found '{declared_id}'")
            prs = data.get("upstream_prs") or []
            if not isinstance(prs, list) or any(not isinstance(pr, int) or isinstance(pr, bool) or pr <= 0 for pr in prs):
                raise ProjectModelError(f"{path}:upstream_prs: expected positive integers")
            env_flag = data.get("env_flag")
            if env_flag is not None:
                env_flag = _string(path, "env_flag", env_flag)
            patches.append(PatchDefinition(
                mod=mod,
                patch_id=patch_id,
                title=_string(path, "title", data.get("title")),
                patch_path=patch_files[patch_id],
                sidecar_path=path,
                requires=_strings(path, "requires", data.get("requires")),
                upstream_prs=tuple(prs),
                env_flag=env_flag,
                build_src_filter=_strings(path, "build_src_filter", data.get("build_src_filter")),
            ))
        return tuple(patches)

    def _validate(self) -> None:
        if not self.roles:
            raise ProjectModelError(f"{self.root / 'build-targets.yaml'}:roles: at least one role is required")

        prefixes: dict[str, str] = {}
        for role in self.roles.values():
            owner = prefixes.setdefault(role.upstream_tag_prefix, role.role_id)
            if owner != role.role_id:
                raise ProjectModelError(
                    f"{self.root / 'build-targets.yaml'}:roles.{role.role_id}.upstream_tag_prefix: also belongs to '{owner}'"
                )

        identities = set()
        environments = set()
        assets = set()
        for target in self.targets:
            identity = (target.board, target.role)
            if identity in identities:
                raise ProjectModelError(f"{self.root / 'build-targets.yaml'}:targets: duplicate target {identity}")
            identities.add(identity)
            if target.build_env in environments:
                raise ProjectModelError(f"{self.root / 'build-targets.yaml'}:targets: duplicate build_env '{target.build_env}'")
            environments.add(target.build_env)
            if target.role not in self.roles:
                raise ProjectModelError(f"{self.root / 'build-targets.yaml'}:targets: unknown role '{target.role}'")
            asset = f"{target.board}_{self.roles[target.role].asset_role_abbrev}_mobmesh"
            if asset in assets:
                raise ProjectModelError(f"{self.root / 'build-targets.yaml'}:targets: duplicate asset_basename '{asset}'")
            assets.add(asset)
            if target.qemu_boot_check and not self.boards[target.board].qemu.enabled:
                raise ProjectModelError(f"{self.boards[target.board].source_path}:qemu.enabled: required by target {identity}")

        for board in self.boards.values():
            unknown_roles = set(board.flasher.post_flash_commands) - set(self.roles)
            if unknown_roles:
                raise ProjectModelError(
                    f"{board.source_path}:flasher.post_flash_commands: unknown role(s): {sorted(unknown_roles)}"
                )

        known_patches = {patch.qualified_id for mod in self.mods.values() for patch in mod.patches}
        env_flags: dict[str, str] = {}
        bits: dict[int, str] = {}
        markers: dict[str, str] = {}
        for mod in self.mods.values():
            for patch in mod.patches:
                for requirement in patch.requires:
                    if requirement not in known_patches:
                        raise ProjectModelError(f"{patch.sidecar_path}:requires: unknown patch '{requirement}'")
            for flag in mod.env_flags + tuple(p.env_flag for p in mod.patches if p.env_flag):
                owner = env_flags.setdefault(flag, mod.name)
                if owner != mod.name:
                    raise ProjectModelError(f"{mod.source_path}:env_flags: '{flag}' also belongs to '{owner}'")
            if mod.bit is not None:
                owner = bits.setdefault(mod.bit, mod.name)
                if owner != mod.name:
                    raise ProjectModelError(f"{mod.source_path}:bit: {mod.bit} also belongs to '{owner}'")
                marker_owner = markers.setdefault(mod.image_marker, mod.name)
                if marker_owner != mod.name:
                    raise ProjectModelError(f"{mod.source_path}:image_marker: also belongs to '{marker_owner}'")

        for board_id in self.boards:
            group = [target for target in self.targets if target.board == board_id]
            vendors = [target for target in group if target.vendor_flasher_assets]
            if len(vendors) != 1:
                raise ProjectModelError(f"{self.root / 'build-targets.yaml'}:targets: board '{board_id}' needs exactly one vendor_flasher_assets target")

    def resolved_mod_names(self, target: TargetDefinition) -> tuple[str, ...]:
        names = list(self.core_mods)
        for name in target.mods:
            if name not in names:
                names.append(name)
        return tuple(names)

    def validate_mod_selection(self, names: tuple[str, ...] | list[str]) -> None:
        applied: set[str] = set()
        for name in names:
            if name not in self.mods:
                raise ProjectModelError(f"unknown selected mod '{name}'")
            for patch in self.mods[name].patches:
                missing = [requirement for requirement in patch.requires if requirement not in applied]
                if missing:
                    raise ProjectModelError(
                        f"{patch.sidecar_path}:requires: {missing} must be applied before {patch.qualified_id}"
                    )
                applied.add(patch.qualified_id)

    def _resolve(self) -> BuildPlan:
        rows = []
        for target in self.targets:
            role = self.roles[target.role]
            board = self.boards[target.board]
            mod_names = self.resolved_mod_names(target)
            if not mod_names:
                raise ProjectModelError(f"{self.root / 'build-targets.yaml'}:targets: {target.board}/{target.role} resolves to no mods")
            self.validate_mod_selection(mod_names)
            for mod_name in mod_names:
                mod = self.mods[mod_name]
                missing = [cap.value for cap in mod.required_capabilities if not board.capability(cap).satisfies_requirement]
                if missing:
                    raise ProjectModelError(
                        f"{self.root / 'build-targets.yaml'}:targets: {target.board}/{target.role} selects '{mod_name}' but board capabilities do not satisfy {missing}"
                    )
            rows.append(ResolvedTarget(
                target_id=target.role,
                board_id=target.board,
                role=target.role,
                build_env=target.build_env,
                upstream_tag_prefix=role.upstream_tag_prefix,
                release_title=role.release_title,
                asset_basename=f"{target.board}_{role.asset_role_abbrev}_mobmesh",
                vendor_flasher_assets=target.vendor_flasher_assets,
                make_latest=role.make_latest,
                mods=mod_names,
                build_flags_append=target.build_flags_append,
                qemu_boot_check=target.qemu_boot_check,
                capabilities={capability.value: board.capability(capability).value for capability in Capability},
            ))
        return BuildPlan(self.core_mods, tuple(rows))

    def ordered_mods(self) -> tuple[str, ...]:
        names = list(self.core_mods)
        for target in self.targets:
            for name in target.mods:
                if name not in names:
                    names.append(name)
        return tuple(names)

    def upstream_pr_entries(self) -> tuple[str, ...]:
        return tuple(
            f"{patch.qualified_id}:{pr}"
            for name in self.ordered_mods()
            for patch in self.mods[name].patches
            for pr in patch.upstream_prs
        )
