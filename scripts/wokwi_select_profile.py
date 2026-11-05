#!/usr/bin/env python3
"""
Select active Wokwi profile by copying profile files to project root.

Usage:
  python3 scripts/wokwi_select_profile.py flowio
  python3 scripts/wokwi_select_profile.py waveshare
  python3 scripts/wokwi_select_profile.py supervisor

This script can also be used as a PlatformIO extra_script. In that mode, it
infers the target profile from the active environment name.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

try:
    Import("env")  # type: ignore[name-defined]
    PIO_ENV = env
except Exception:
    PIO_ENV = None


PROFILE_FILES = ("diagram.json", "wokwi.toml")
ENV_PROFILE_MAP = {
    "FlowIOWokwi": "flowio",
    "WaveshareWokwi": "waveshare",
    "SupervisorWokwi": "supervisor",
}


def project_dir() -> Path:
    if PIO_ENV is not None:
        project_dir_value = PIO_ENV.get("PROJECT_DIR")
        if project_dir_value:
            return Path(str(project_dir_value))
    if "__file__" in globals():
        return Path(__file__).resolve().parent.parent
    return Path.cwd()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Select active Wokwi profile")
    parser.add_argument(
        "profile",
        choices=("flowio", "waveshare", "supervisor"),
        help="Profile name under wokwi/<profile>/",
    )
    return parser.parse_args()


def apply_profile(profile: str) -> int:
    root_dir = project_dir()
    profile_dir = root_dir / "wokwi" / profile

    if not profile_dir.exists():
        print(f"[wokwi-profile] missing profile directory: {profile_dir}", file=sys.stderr)
        return 2

    for name in PROFILE_FILES:
        src = profile_dir / name
        dst = root_dir / name
        if not src.exists():
            print(f"[wokwi-profile] missing source file: {src}", file=sys.stderr)
            return 2
        shutil.copy2(src, dst)

    print(f"[wokwi-profile] active profile set to '{profile}'")
    print(f"[wokwi-profile] updated: {root_dir / 'diagram.json'}")
    print(f"[wokwi-profile] updated: {root_dir / 'wokwi.toml'}")
    return 0


def profile_from_platformio_env() -> str | None:
    if PIO_ENV is None:
        return None

    pioenv = PIO_ENV.get("PIOENV")
    if not pioenv:
        return None
    return ENV_PROFILE_MAP.get(str(pioenv))


def main() -> int:
    inferred_profile = profile_from_platformio_env()
    if inferred_profile is not None:
        return apply_profile(inferred_profile)

    args = parse_args()
    return apply_profile(args.profile)


if PIO_ENV is not None:
    inferred_profile = profile_from_platformio_env()
    if inferred_profile is None:
        raise RuntimeError("[wokwi-profile] unable to infer profile from PIOENV")
    rc = apply_profile(inferred_profile)
    if rc != 0:
        raise RuntimeError(f"[wokwi-profile] failed with exit code {rc}")


if __name__ == "__main__":
    raise SystemExit(main())
