#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build isolated host-policy fixtures from an explicit unchanged resolved core."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from ci_support import load_pins, workspace_identity
from integration_checks import rejected_peripheral_warning, rejected_split_warning

root = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--workspace", type=Path, required=True)
ap.add_argument("--output", type=Path, required=True)
ap.add_argument(
    "--build-root",
    type=Path,
    help="Fresh Linux build root, independent of retained evidence",
)
a = ap.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
if a.build_root is not None:
    a.build_root.mkdir(parents=True, exist_ok=False)
w = a.workspace.resolve()
env = dict(
    os.environ,
    GIT_NO_LAZY_FETCH="1",
    ZEPHYR_BASE=str(w / "zephyr"),
    Zephyr_DIR=str(w / "zephyr/share/zephyr-package/cmake"),
    CMAKE_BUILD_PARALLEL_LEVEL=os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", "2"),
)
selected = subprocess.check_output(
    ["west", "list", "-f", "{name}\t{abspath}"], cwd=w, env=env, text=True
).splitlines()
modules = []
identities = []
for line in selected:
    name, path = line.split("\t")
    path = Path(path)
    if not path.is_dir():
        continue
    if name == "manifest":
        manifest = path / "west.yml"
        identities.append(
            dict(
                name=name,
                path=str(path),
                kind="owner-manifest",
                sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),
            )
        )
        continue
    head = subprocess.run(
        [
            "git",
            "-c",
            "safe.directory=" + str(path),
            "-C",
            str(path),
            "rev-parse",
            "HEAD",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    identities.append(
        dict(
            name=name,
            path=str(path),
            revision=head.stdout.strip(),
            exit=head.returncode,
        )
    )
    assert head.returncode == 0, (name, "Unreadable dependency identity")
    if name not in ("zmk", "zephyr") and (
        (path / "zephyr/module.yml").is_file()
        or (path / "zephyr/CMakeLists.txt").is_file()
    ):
        modules.append(str(path))
(a.output / "dependencies.json").write_text(json.dumps(identities, indent=2))
assert not any("messenger" in x or "ble-management" in x for x in modules), (
    "Isolation fixture must not import application owners"
)
modules.append(str(root))
core_revision = next(x["revision"] for x in identities if x["name"] == "zmk")
assert core_revision in (
    "5b51501fead672c41b5cfb396f3dafe0894bf4e9",
    "9f75b536cff5a63d34cba43de9be8652b8bd1915",
), core_revision
pins = load_pins()
core = next(
    name
    for name, projects in pins["cores"].items()
    if next(p["revision"] for p in projects if p["name"] == "zmk") == core_revision
)
(a.output / "required-dependencies.json").write_text(
    json.dumps(workspace_identity(w, core, pins), indent=2) + "\n", encoding="utf-8"
)
rows = []
for mode in (
    "enabled",
    "diagnostics",
    "studio",
    "disabled",
    "peripheral",
    "reject-peripheral",
    "split-only",
    "host-and-split",
    "split-disabled",
    "reject-split-peripheral",
    "reject-split-multi",
):
    out = a.output / mode
    out.mkdir()
    build = (a.build_root / mode) if a.build_root is not None else out / "build"
    enabled = mode in ("enabled", "diagnostics", "studio", "host-and-split", "split-disabled")
    requested = enabled or mode == "reject-peripheral"
    split_enabled = mode in ("split-only", "host-and-split")
    split_requested = split_enabled or mode in ("reject-split-peripheral", "reject-split-multi")
    conf = out / "fixture.conf"
    text = (
        "CONFIG_ZMK_BLE=y\nCONFIG_SETTINGS=y\nCONFIG_ZMK_STUDIO="
        + ("y" if mode == "studio" else "n")
        + "\nCONFIG_ZMK_BLE_LINK_POLICY="
        + ("y" if requested else "n")
        + "\n"
    )
    text += "CONFIG_ZMK_BLE_SPLIT_POLICY=" + ("y" if split_requested else "n") + "\n"
    if mode == "diagnostics":
        text += "CONFIG_LOG=y\nCONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS=y\n"
    if mode in ("peripheral", "reject-peripheral", "reject-split-peripheral"):
        text += "CONFIG_ZMK_SPLIT=y\nCONFIG_ZMK_SPLIT_ROLE_CENTRAL=n\n"
    elif mode in ("split-only", "host-and-split", "split-disabled", "reject-split-multi"):
        text += "CONFIG_ZMK_SPLIT=y\nCONFIG_ZMK_SPLIT_ROLE_CENTRAL=y\n"
    if mode == "reject-split-multi":
        text += "CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=2\n"
    conf.write_text(text)
    command = [
        "west",
        "build",
        "-s",
        str(w / "zmk/app"),
        "-d",
        str(build),
        "-b",
        "xiao_ble/nrf52840/zmk",
        "--",
        "-DSHIELD=tester_xiao",
        "-DEXTRA_CONF_FILE=" + str(conf),
        "-DZEPHYR_BASE=" + str(w / "zephyr"),
        "-DZEPHYR_MODULES=" + ";".join(modules),
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    ]
    start = time.time()
    with (out / "build.log").open("w") as log:
        built = subprocess.run(
            command, cwd=w, env=env, stdout=log, stderr=subprocess.STDOUT
        )
    row = dict(
        mode=mode, command=command, exit=built.returncode, start=start, end=time.time()
    )
    rows.append(row)
    (a.output / "results.json").write_text(json.dumps(rows, indent=2))
    if mode == "reject-peripheral":
        log = (out / "build.log").read_text()
        assert rejected_peripheral_warning(log), mode
    if mode in ("reject-split-peripheral", "reject-split-multi"):
        dependency = ("ZMK_SPLIT_ROLE_CENTRAL" if mode == "reject-split-peripheral"
                      else "ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS")
        assert rejected_split_warning((out / "build.log").read_text(), dependency), mode
    # A rejected requested value is a warning on these cores; prove the actual
    # built config, objects and symbols remain disabled as well.
    assert built.returncode == 0, mode
    if built.returncode == 0:
        config = (build / "zephyr/.config").read_text()
        assert ("CONFIG_ZMK_BLE_LINK_POLICY=y" in config.splitlines()) == enabled, mode
        assert ("CONFIG_ZMK_BLE_LINK_POLICY_DIAGNOSTICS=y" in config.splitlines()) == (
            mode == "diagnostics"
        ), mode
        assert ("CONFIG_ZMK_STUDIO=y" in config.splitlines()) == (mode == "studio"), (
            mode
        )
        assert ("CONFIG_ZMK_BLE_SPLIT_POLICY=y" in config.splitlines()) == split_enabled, mode
        if split_enabled:
            assert "CONFIG_ZMK_BLE_SPLIT_POLICY_IDLE_INTERVAL=24" in config.splitlines(), mode
            for value in ["CONFIG_ZMK_SPLIT_BLE_PREF_INT=6", "CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY=30",
                          "CONFIG_ZMK_SPLIT_BLE_PREF_TIMEOUT=400"]:
                assert value in config.splitlines(), (mode, value)
        assert "CONFIG_ZMK_BLE_MESSENGER=y" not in config, mode
        compiled = {
            x["file"] for x in json.loads((build / "compile_commands.json").read_text())
        }
        for name in ("link_service.c", "link_policy.c"):
            assert (str(root / "src" / name) in compiled) == enabled, (mode, name)
        assert (str(root / "src/split_service.c") in compiled) == split_enabled, mode
        elf = build / "zephyr/zmk.elf"
        cache = (build / "CMakeCache.txt").read_text()
        compiler = next(
            line.split("=", 1)[1]
            for line in cache.splitlines()
            if line.startswith("CMAKE_C_COMPILER:")
        )
        nm_tool = str(Path(compiler).with_name("arm-zephyr-eabi-nm"))
        nm = subprocess.check_output([nm_tool, str(elf)], text=True)
        (out / "symbols.txt").write_text(nm)
        assert ("zblp_link_next" in nm) == enabled, mode
        assert ("bt_conn_cb_zblp_split_callbacks" in nm) == split_enabled, mode
        row["hashes"] = {
            str(f): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [elf, build / "zephyr/.config", build / "zephyr/zephyr.dts"]
        }
        (a.output / "results.json").write_text(json.dumps(rows, indent=2))
    print(mode, "PASS", flush=True)

(a.output / "dependencies-after.json").write_text(
    json.dumps(workspace_identity(w, core, pins), indent=2) + "\n", encoding="utf-8"
)
