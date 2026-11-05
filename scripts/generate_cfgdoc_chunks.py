#!/usr/bin/env python3
"""Generate segmented cfgdoc assets from legacy cfgdocs/cfgmods.

Outputs (SPIFFS-short names):
- data/wc/i.j
- data/wc/mXXXXXXXX.j
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

PROJECT_ROOT = Path(__file__).resolve().parents[1]
WEB_DIR = PROJECT_ROOT / "data" / "webinterface"
CFGDOC_DIR = PROJECT_ROOT / "data" / "wc"
SRC_MODULES_DIR = PROJECT_ROOT / "src" / "Modules"


def _load_json(path: Path) -> dict:
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def _merge_meta(base_meta: dict, overlay_meta: dict) -> dict:
    out = dict(base_meta or {})

    def merge_dict(dst: dict, src: dict) -> dict:
        merged = dict(dst or {})
        for key, value in (src or {}).items():
            if isinstance(value, dict) and isinstance(merged.get(key), dict):
                merged[key] = merge_dict(merged[key], value)
            else:
                merged[key] = value
        return merged

    out = merge_dict(out, overlay_meta or {})

    # Merge list-like metadata used by the tree helper without duplicating entries.
    for list_key in ("cfg_tree_aliases", "cfg_tree_virtual_branches"):
        merged_list: List[dict] = []
        seen = set()
        for source in (base_meta or {}, overlay_meta or {}):
            values = source.get(list_key)
            if not isinstance(values, list):
                continue
            for item in values:
                token = json.dumps(item, ensure_ascii=False, sort_keys=True)
                if token in seen:
                    continue
                seen.add(token)
                merged_list.append(item)
        if merged_list:
            out[list_key] = merged_list

    return out


def _normalized_meta(payload: dict) -> dict:
    if not isinstance(payload, dict):
        return {}
    meta = payload.get("meta")
    if isinstance(meta, dict):
        return meta
    legacy = payload.get("_meta")
    return legacy if isinstance(legacy, dict) else {}


def _module_key_for_doc_key(key: str, all_keys: Iterable[str]) -> str:
    clean = str(key or "").strip()
    if not clean:
        return "__root"
    if clean.startswith("*/"):
        return "__wildcard"
    if "/" not in clean:
        return "__root"
    has_children = any(other != clean and str(other).startswith(clean + "/") for other in all_keys)
    if has_children:
        return clean
    return clean.rsplit("/", 1)[0]


def _fnv1a_32(text: str) -> int:
    h = 0x811C9DC5
    for b in text.encode("utf-8", errors="ignore"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def _safe_module_file_name(module_key: str) -> str:
    digest = _fnv1a_32(module_key.strip().lower())
    return f"m{digest:08x}.j"


def _merge_translations(base: Dict[str, str], overlay: Dict[str, str]) -> Dict[str, str]:
    out = dict(base or {})
    for key, value in (overlay or {}).items():
        if not isinstance(key, str) or not isinstance(value, str):
            continue
        clean_key = key.strip()
        if not clean_key:
            continue
        out[clean_key] = value
    return out


def _merge_doc_entry(base: dict, overlay: dict) -> dict:
    out = dict(base or {})
    for key, value in (overlay or {}).items():
        if isinstance(key, str):
            out[key] = value
    return out


def _load_text_i18n_payload(path: Path) -> Dict[str, str]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}
    if not isinstance(payload, dict):
        return {}
    translations = payload.get("translations")
    source = translations if isinstance(translations, dict) else payload
    out: Dict[str, str] = {}
    for raw_key, raw_value in source.items():
        if isinstance(raw_key, str) and isinstance(raw_value, str):
            clean_key = raw_key.strip()
            if clean_key:
                out[clean_key] = raw_value
    return out


def _collect_consolidated_i18n() -> Dict[str, Dict[str, str]]:
    if not SRC_MODULES_DIR.exists():
        return {}
    per_locale: Dict[str, Dict[str, str]] = {}

    # `text/i18n.<locale>.json`
    for path in sorted(SRC_MODULES_DIR.rglob("text/i18n.*.json")):
        suffix = path.name[len("i18n.") : -len(".json")].strip().lower()
        if not suffix:
            continue
        per_locale[suffix] = _merge_translations(per_locale.get(suffix, {}), _load_text_i18n_payload(path))

    # Optional locale-agnostic fallback: `text/i18n.json` (applied to all locales).
    generic_files = sorted(SRC_MODULES_DIR.rglob("text/i18n.json"))
    generic: Dict[str, str] = {}
    for path in generic_files:
        generic = _merge_translations(generic, _load_text_i18n_payload(path))

    if generic:
        if not per_locale:
            per_locale["fr"] = dict(generic)
        else:
            for locale in list(per_locale.keys()):
                merged = dict(generic)
                merged.update(per_locale.get(locale, {}))
                per_locale[locale] = merged

    if "fr" not in per_locale:
        per_locale["fr"] = {}
    return per_locale


def main() -> int:
    cfgdocs = _load_json(WEB_DIR / "cfgdocs.json")
    cfgmods = _load_json(WEB_DIR / "cfgmods.json")

    docs_map: Dict[str, dict] = {}
    for source in (cfgdocs, cfgmods):
        docs = source.get("docs") if isinstance(source, dict) else None
        if not isinstance(docs, dict):
            continue
        for key, value in docs.items():
            if isinstance(value, dict):
                clean_key = str(key)
                docs_map[clean_key] = _merge_doc_entry(docs_map.get(clean_key, {}), value)

    combined_meta = _merge_meta(_normalized_meta(cfgdocs), _normalized_meta(cfgmods))

    keys = list(docs_map.keys())
    modules: Dict[str, Dict[str, dict]] = {}
    for key in keys:
        module_key = _module_key_for_doc_key(key, keys)
        modules.setdefault(module_key, {})[key] = docs_map[key]

    CFGDOC_DIR.mkdir(parents=True, exist_ok=True)
    for path in CFGDOC_DIR.glob("*"):
        path.unlink()

    modules_index: Dict[str, str] = {}
    for module_key in sorted(modules.keys()):
        file_name = _safe_module_file_name(module_key)
        payload = {
            "ok": True,
            "module": module_key,
            "docs": modules[module_key],
            "meta": {},
        }
        out_file = CFGDOC_DIR / file_name
        out_file.write_text(json.dumps(payload, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
        modules_index[module_key] = file_name

    i18n_by_locale = _collect_consolidated_i18n()
    locales = sorted(i18n_by_locale.keys())
    for locale, translations in i18n_by_locale.items():
        i18n_payload = {
            "ok": True,
            "locale": locale,
            "generated": True,
            "translations": translations,
        }
        (CFGDOC_DIR / f"i18n.{locale}.j").write_text(
            json.dumps(i18n_payload, ensure_ascii=False, separators=(",", ":")),
            encoding="utf-8",
        )

    index_payload = {
        "ok": True,
        "version": "cfgdoc-chunks-v1",
        "meta": combined_meta,
        "docs": {},
        "modules": modules_index,
        "locales": locales,
    }
    (CFGDOC_DIR / "i.j").write_text(
        json.dumps(index_payload, ensure_ascii=False, separators=(",", ":")),
        encoding="utf-8",
    )

    print(f"[cfgdoc] modules={len(modules_index)} output={CFGDOC_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
