#!/usr/bin/env python3
"""Migrate global cfgdocs/cfgmods into per-module text + i18n manifests."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple

PROJECT_ROOT = Path(__file__).resolve().parents[1]
WEB_DIR = PROJECT_ROOT / "data" / "webinterface"
MODULES_DIR = PROJECT_ROOT / "src" / "Modules"
LOCALE = "fr"

DOC_ALLOWED_FIELDS = (
    "type",
    "label",
    "help",
    "unit",
    "display_format",
    "enum_set",
    "hidden",
    "apply_per_field",
)

MODULE_RULES: List[Tuple[re.Pattern[str], str]] = [
    (re.compile(r"^\*/"), "PoolDeviceModule"),
    (re.compile(r"^alarms/"), "AlarmModule"),
    (re.compile(r"^elink/client/"), "Network/I2CCfgClientModule"),
    (re.compile(r"^elink/server/"), "Network/I2CCfgServerModule"),
    (re.compile(r"^elink/lcd/"), "SupervisorHMIModule"),
    (re.compile(r"^fcd/"), "FlowConnectDisplay/FlowConnectDisplayUdpClientModule"),
    (re.compile(r"^fwupdate/"), "Network/FirmwareUpdateModule"),
    (re.compile(r"^ha/"), "Network/HAModule"),
    (re.compile(r"^hmi/nextion_udp/"), "Network/HmiUdpServerModule"),
    (re.compile(r"^hmi(?:/|$)"), "HMIModule"),
    (re.compile(r"^io/"), "IOModule"),
    (re.compile(r"^log(?:/|$)"), "Logs/LogHubModule"),
    (re.compile(r"^mqtt/"), "Network/MQTTModule"),
    (re.compile(r"^network/mqtt(?:/|$)"), "Network/MQTTModule"),
    (re.compile(r"^network/time(?:/|$)"), "Network/TimeModule"),
    (re.compile(r"^network(?:/wifi)?(?:/|$)"), "Network/WifiModule"),
    (re.compile(r"^pdm(?:/|$)"), "PoolDeviceModule"),
    (re.compile(r"^poollogic(?:/|$)"), "PoolLogicModule"),
    (re.compile(r"^sysmon(?:/|$)"), "System/SystemMonitorModule"),
    (re.compile(r"^time(?:/|$)"), "Network/TimeModule"),
    (re.compile(r"^wifi(?:/|$)"), "Network/WifiModule"),
]

FALLBACK_MODULE = "System/SystemModule"


def _load_json(path: Path) -> dict:
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    return data if isinstance(data, dict) else {}


def _module_dirs() -> List[str]:
    module_dirs: Set[str] = set()
    for header in sorted(MODULES_DIR.rglob("*Module.h")):
        rel = header.parent.relative_to(MODULES_DIR).as_posix()
        module_dirs.add(rel)
    return sorted(module_dirs)


def _module_dir_for_key(key: str) -> str:
    clean = str(key or "").strip().strip("/")
    for rule, module_dir in MODULE_RULES:
        if rule.search(clean):
            return module_dir
    return FALLBACK_MODULE


def _norm_token_part(raw: str) -> str:
    s = raw.strip().lower().replace("/", ".").replace("*", "wildcard")
    s = re.sub(r"[^a-z0-9._-]+", "_", s)
    s = re.sub(r"_+", "_", s).strip("._")
    return s or "key"


def _token_for(doc_kind: str, key: str, field: str) -> str:
    return f"{doc_kind}.{_norm_token_part(key)}.{field}"


def _copy_doc_for_manifest(doc_kind: str, key: str, value: dict, translations: Dict[str, str]) -> dict:
    out: Dict[str, object] = {}
    for field in DOC_ALLOWED_FIELDS:
        if field not in value:
            continue
        if field in ("label", "help") and isinstance(value[field], str) and value[field].strip():
            tok = _token_for(doc_kind, key, field)
            out[f"{field}_t"] = tok
            translations[tok] = value[field].strip()
            continue
        out[field] = value[field]
    return out


def _tokenize_meta_i18n(
    node: object,
    token_root: str,
    translations: Dict[str, str],
    path_parts: Optional[List[str]] = None,
) -> object:
    parts = list(path_parts or [])
    if isinstance(node, list):
        out_list: List[object] = []
        for idx, item in enumerate(node):
            out_list.append(_tokenize_meta_i18n(item, token_root, translations, parts + [str(idx)]))
        return out_list
    if not isinstance(node, dict):
        return node

    out: Dict[str, object] = {}
    for key, value in node.items():
        if key in ("label", "help") and isinstance(value, str) and value.strip():
            token_stem = ".".join(parts) if parts else "meta"
            token = f"{token_root}.{_norm_token_part(token_stem)}.{key}"
            out[f"{key}_t"] = token
            translations[token] = value.strip()
            continue
        out[key] = _tokenize_meta_i18n(value, token_root, translations, parts + [str(key)])
    return out


def _enum_sets_used_by_docs(docs: Dict[str, dict]) -> Set[str]:
    used: Set[str] = set()
    for value in docs.values():
        if not isinstance(value, dict):
            continue
        enum_set = value.get("enum_set")
        if isinstance(enum_set, str) and enum_set.strip():
            used.add(enum_set.strip())
    return used


def _write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    cfgdocs = _load_json(WEB_DIR / f"cfgdocs.{LOCALE}.json")
    cfgmods = _load_json(WEB_DIR / f"cfgmods.{LOCALE}.json")
    cfgdocs_docs = cfgdocs.get("docs") if isinstance(cfgdocs.get("docs"), dict) else {}
    cfgmods_docs = cfgmods.get("docs") if isinstance(cfgmods.get("docs"), dict) else {}

    cfgmods_meta = cfgmods.get("meta") if isinstance(cfgmods.get("meta"), dict) else cfgmods.get("_meta", {})
    if not isinstance(cfgmods_meta, dict):
        cfgmods_meta = {}
    all_enum_sets = cfgmods_meta.get("enum_sets")
    if not isinstance(all_enum_sets, dict):
        all_enum_sets = {}

    module_dirs = _module_dirs()
    if FALLBACK_MODULE not in module_dirs:
        module_dirs.append(FALLBACK_MODULE)
    module_dirs = sorted(set(module_dirs))

    module_cfgdocs: Dict[str, Dict[str, dict]] = {m: {} for m in module_dirs}
    module_cfgmods: Dict[str, Dict[str, dict]] = {m: {} for m in module_dirs}
    module_i18n: Dict[str, Dict[str, str]] = {m: {} for m in module_dirs}

    for key, value in cfgdocs_docs.items():
        if not isinstance(key, str) or not isinstance(value, dict):
            continue
        module_dir = _module_dir_for_key(key)
        module_cfgdocs.setdefault(module_dir, {})
        module_i18n.setdefault(module_dir, {})
        doc = _copy_doc_for_manifest("cfgdocs", key, value, module_i18n[module_dir])
        if doc:
            module_cfgdocs[module_dir][key.strip("/")] = doc

    for key, value in cfgmods_docs.items():
        if not isinstance(key, str) or not isinstance(value, dict):
            continue
        module_dir = _module_dir_for_key(key)
        module_cfgmods.setdefault(module_dir, {})
        module_i18n.setdefault(module_dir, {})
        doc = _copy_doc_for_manifest("cfgmods", key, value, module_i18n[module_dir])
        if doc:
            module_cfgmods[module_dir][key.strip("/")] = doc

    written_cfgdocs = 0
    written_cfgmods = 0
    written_i18n = 0

    for module_dir in module_dirs:
        text_dir = MODULES_DIR / module_dir / "text"
        cfgdocs_docs_out = module_cfgdocs.get(module_dir, {})
        cfgmods_docs_out = module_cfgmods.get(module_dir, {})

        cfgdocs_payload = {
            "_meta": {"generated": True, "locale": LOCALE, "version": 1},
            "docs": dict(sorted(cfgdocs_docs_out.items(), key=lambda kv: kv[0])),
        }
        _write_json(text_dir / f"cfgdocs.{LOCALE}.json", cfgdocs_payload)
        written_cfgdocs += 1

        cfgmods_payload = {
            "_meta": {"generated": True, "locale": LOCALE, "version": 1},
            "docs": dict(sorted(cfgmods_docs_out.items(), key=lambda kv: kv[0])),
        }

        cfgmods_meta_out: Dict[str, object] = {}
        used_sets = _enum_sets_used_by_docs(cfgmods_docs_out) | _enum_sets_used_by_docs(cfgdocs_docs_out)
        enum_meta = {
            name: all_enum_sets[name]
            for name in sorted(used_sets)
            if name in all_enum_sets and isinstance(all_enum_sets[name], list)
        }
        if enum_meta:
            cfgmods_meta_out["enum_sets"] = enum_meta

        if module_dir == "Network/WifiModule":
            aliases = cfgmods_meta.get("cfg_tree_aliases")
            branches = cfgmods_meta.get("cfg_tree_virtual_branches")
            if isinstance(aliases, list):
                cfgmods_meta_out["cfg_tree_aliases"] = aliases
            if isinstance(branches, list):
                cfgmods_meta_out["cfg_tree_virtual_branches"] = branches

        if cfgmods_meta_out:
            cfgmods_meta_out = _tokenize_meta_i18n(
                cfgmods_meta_out,
                token_root=f"cfgmods_meta.{_norm_token_part(module_dir)}",
                translations=module_i18n.setdefault(module_dir, {}),
            )

        if cfgmods_meta_out:
            cfgmods_payload["meta"] = cfgmods_meta_out

        _write_json(text_dir / f"cfgmods.{LOCALE}.json", cfgmods_payload)
        written_cfgmods += 1

        i18n_payload = {
            "translations": dict(sorted(module_i18n.get(module_dir, {}).items(), key=lambda kv: kv[0]))
        }
        _write_json(text_dir / f"i18n.{LOCALE}.json", i18n_payload)
        written_i18n += 1

    print(
        "[migrate_text_manifests] "
        f"modules={len(module_dirs)} cfgdocs_files={written_cfgdocs} "
        f"cfgmods_files={written_cfgmods} i18n_files={written_i18n}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
