#!/usr/bin/env python3
"""Generate cfgdocs/cfgmods payloads from module text manifests."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

Import = type("Import", (), {})

try:
    Import("env")  # type: ignore
except Exception:
    env = None


def _get_project_dir() -> Path:
    if env is not None:
        try:
            return Path(env.get("PROJECT_DIR"))
        except Exception:
            pass
    return Path(os.getcwd())


def _merge_meta_dict(base: dict, overlay: dict) -> dict:
    out = dict(base or {})
    if not isinstance(overlay, dict):
        return out
    for key, value in overlay.items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = _merge_meta_dict(out[key], value)
            continue
        if isinstance(value, list) and isinstance(out.get(key), list):
            merged: List[Any] = []
            seen = set()
            for source in (out.get(key, []), value):
                for item in source:
                    token = json.dumps(item, ensure_ascii=False, sort_keys=True)
                    if token in seen:
                        continue
                    seen.add(token)
                    merged.append(item)
            out[key] = merged
            continue
        out[key] = value
    return out


def _load_text_payload(path: Path) -> Optional[dict]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"[generate_config_docs] warning: invalid JSON in {path}: {exc}")
        return None
    if not isinstance(data, dict):
        print(f"[generate_config_docs] warning: ignored non-object payload in {path}")
        return None
    return data


def _text_manifest_files(src_root: Path, stem: str, locale: str = "fr") -> List[Path]:
    modules_root = src_root / "Modules"
    if not modules_root.exists():
        return []
    candidates = sorted(modules_root.rglob(f"text/{stem}*.json"))
    by_dir: Dict[Path, List[Path]] = {}
    for path in candidates:
        by_dir.setdefault(path.parent, []).append(path)

    selected: List[Path] = []
    target_name = f"{stem}.{locale}.json"
    for _, paths in sorted(by_dir.items(), key=lambda item: str(item[0])):
        picks = {path.name: path for path in paths}
        chosen = (
            picks.get(target_name)
            or picks.get(f"{stem}.json")
            or picks.get(f"{stem}.fr.json")
            or (sorted(paths)[0] if paths else None)
        )
        if chosen:
            selected.append(chosen)
    return selected


def _load_text_docs(src_root: Path, stem: str, locale: str = "fr") -> Tuple[Dict[str, dict], dict, List[Path]]:
    docs: Dict[str, dict] = {}
    meta: dict = {}
    loaded_files: List[Path] = []
    for path in _text_manifest_files(src_root, stem=stem, locale=locale):
        payload = _load_text_payload(path)
        if payload is None:
            continue
        loaded_files.append(path)
        payload_docs = payload.get("docs")
        if isinstance(payload_docs, dict):
            for raw_key, raw_val in payload_docs.items():
                if not isinstance(raw_key, str) or not isinstance(raw_val, dict):
                    continue
                docs[raw_key.strip("/")] = dict(raw_val)
        payload_meta = payload.get("meta")
        if not isinstance(payload_meta, dict):
            payload_meta = payload.get("_meta")
        if isinstance(payload_meta, dict):
            meta = _merge_meta_dict(meta, payload_meta)
    return docs, meta, loaded_files


def _load_text_translations(src_root: Path, locale: str = "fr") -> Tuple[Dict[str, str], List[Path]]:
    modules_root = src_root / "Modules"
    if not modules_root.exists():
        return {}, []
    out: Dict[str, str] = {}
    loaded_files: List[Path] = []
    for path in sorted(modules_root.rglob(f"text/i18n.{locale}.json")):
        payload = _load_text_payload(path)
        if payload is None:
            continue
        loaded_files.append(path)
        translations = payload.get("translations")
        source = translations if isinstance(translations, dict) else payload
        for raw_key, raw_val in source.items():
            if not isinstance(raw_key, str) or not isinstance(raw_val, str):
                continue
            key = raw_key.strip()
            if key:
                out[key] = raw_val
    return out, loaded_files


def _expand_digital_input_slot_docs(docs: Dict[str, dict], last_slot: int) -> None:
    """Clone the i07 descriptors for scalable digital-input slots."""
    template_prefix = "io/input/i07/"
    templates = [
        (key, value)
        for key, value in docs.items()
        if key.startswith(template_prefix) and isinstance(value, dict)
    ]
    for slot in range(8, last_slot + 1):
        old_lower = "i07"
        new_lower = f"i{slot:02d}"
        old_upper = "I07"
        new_upper = f"I{slot:02d}"
        for key, value in templates:
            new_key = key.replace(old_lower, new_lower)
            if new_key in docs:
                continue
            encoded = json.dumps(value, ensure_ascii=False)
            encoded = encoded.replace(old_lower, new_lower).replace(old_upper, new_upper)
            docs[new_key] = json.loads(encoded)


def _expand_digital_input_slot_translations(translations: Dict[str, str], last_slot: int) -> None:
    """Clone i07 translations used by generated scalable slot descriptors."""
    templates = [
        (key, value)
        for key, value in translations.items()
        if ".i07." in key and isinstance(value, str)
    ]
    for slot in range(8, last_slot + 1):
        old_lower = "i07"
        new_lower = f"i{slot:02d}"
        old_upper = "I07"
        new_upper = f"I{slot:02d}"
        for key, value in templates:
            new_key = key.replace(old_lower, new_lower)
            if new_key in translations:
                continue
            translations[new_key] = value.replace(old_lower, new_lower).replace(old_upper, new_upper)


def _prune_io_slot_docs(docs: Dict[str, dict], analog_last: int, digital_last: int, output_last: int) -> None:
    """Remove descriptors for IO slots not compiled by the selected profile."""
    limits = (
        ("io/input/a", analog_last),
        ("io/input/i", digital_last),
        ("io/output/d", output_last),
    )
    for key in list(docs):
        for prefix, last_slot in limits:
            if not key.startswith(prefix):
                continue
            suffix = key[len(prefix):]
            digits = suffix.split("/", 1)[0]
            if digits.isdigit() and int(digits) > last_slot:
                del docs[key]
            break


def _prune_pool_device_docs(docs: Dict[str, dict], last_slot: int) -> None:
    """Remove pool-device descriptors above the selected profile capacity."""
    prefix = "pdm/pd"
    for key in list(docs):
        if not key.startswith(prefix):
            continue
        suffix = key[len(prefix):]
        digits = suffix.split("/", 1)[0]
        if digits.isdigit() and int(digits) > last_slot:
            del docs[key]


def _prune_io_slot_meta(meta: dict, analog_last: int, digital_last: int, output_last: int) -> dict:
    """Align config-tree aliases and logical-slot enums with profile capacities."""
    if not isinstance(meta, dict):
        return meta

    out = dict(meta)
    aliases = out.get("cfg_tree_aliases")
    if isinstance(aliases, list):
        filtered_aliases = []
        for entry in aliases:
            display = str(entry.get("display", "")) if isinstance(entry, dict) else ""
            keep = True
            for prefix, last_slot in (
                ("io/input/analog/a", analog_last),
                ("io/input/digital/i", digital_last),
                ("io/output/d", output_last),
            ):
                if not display.startswith(prefix):
                    continue
                suffix = display[len(prefix):].split("/", 1)[0]
                keep = not suffix.isdigit() or int(suffix) <= last_slot
                break
            if keep:
                filtered_aliases.append(entry)
        out["cfg_tree_aliases"] = filtered_aliases

    branches = out.get("cfg_tree_virtual_branches")
    if isinstance(branches, list):
        filtered_branches = []
        for entry in branches:
            if not isinstance(entry, dict):
                filtered_branches.append(entry)
                continue
            branch = dict(entry)
            display = str(branch.get("display", ""))
            children = branch.get("children")
            if isinstance(children, list):
                if display == "io/input/analog":
                    branch["children"] = [v for v in children if str(v)[1:].isdigit() and int(str(v)[1:]) <= analog_last]
                elif display == "io/input/digital":
                    branch["children"] = [v for v in children if str(v)[1:].isdigit() and int(str(v)[1:]) <= digital_last]
                elif display == "io/output":
                    branch["children"] = [v for v in children if str(v)[1:].isdigit() and int(str(v)[1:]) <= output_last]
            filtered_branches.append(branch)
        out["cfg_tree_virtual_branches"] = filtered_branches

    enum_sets = out.get("enum_sets")
    if isinstance(enum_sets, dict):
        enum_sets = dict(enum_sets)
        analog_entries = enum_sets.get("flowio_logical_input_analog")
        if isinstance(analog_entries, list):
            enum_sets["flowio_logical_input_analog"] = [
                entry for entry in analog_entries
                if (_to_int(entry.get("value")) is not None and _to_int(entry.get("value")) <= 192 + analog_last)
            ]
        digital_entries = enum_sets.get("flowio_logical_input_digital")
        if isinstance(digital_entries, list):
            enum_sets["flowio_logical_input_digital"] = [
                entry for entry in digital_entries
                if (_to_int(entry.get("value")) is not None and _to_int(entry.get("value")) <= 64 + digital_last)
            ]
        out["enum_sets"] = enum_sets
    return out


def _resolve_doc_i18n_fields(raw_doc: dict, translations: Dict[str, str]) -> dict:
    doc = dict(raw_doc or {})
    label_token = doc.get("label_t")
    help_token = doc.get("help_t")
    if isinstance(label_token, str) and label_token.strip():
        token = label_token.strip()
        doc["label"] = translations.get(token, token)
        doc["label_i18n"] = token
    if isinstance(help_token, str) and help_token.strip():
        token = help_token.strip()
        doc["help"] = translations.get(token, token)
        doc["help_i18n"] = token
    return doc


def _resolve_meta_i18n(node: Any, translations: Dict[str, str]) -> Any:
    if isinstance(node, list):
        return [_resolve_meta_i18n(item, translations) for item in node]
    if not isinstance(node, dict):
        return node
    out = {k: _resolve_meta_i18n(v, translations) for k, v in node.items()}
    label_token = out.get("label_t")
    help_token = out.get("help_t")
    if isinstance(label_token, str) and label_token.strip():
        token = label_token.strip()
        out["label"] = translations.get(token, token)
        out["label_i18n"] = token
    if isinstance(help_token, str) and help_token.strip():
        token = help_token.strip()
        out["help"] = translations.get(token, token)
        out["help_i18n"] = token
    return out


def _detect_pio_env() -> str:
    if env is not None:
        try:
            value = str(env.subst("$PIOENV") or "").strip()
            if value:
                return value
        except Exception:
            pass
    return str(os.getenv("PIOENV", "") or "").strip()


def _profile_from_pio_env(pio_env: str) -> str:
    name = str(pio_env or "").strip().lower()
    if "waveshare" in name:
        return "waveshare"
    if name.startswith("flowio") or "flowio" in name:
        return "flowio"
    return "generic"


def _profile_override_from_project_options() -> Optional[str]:
    from_env = str(os.getenv("FLOW_CFGDOC_PROFILE", "") or "").strip().lower()
    if from_env in ("flowio", "waveshare", "generic"):
        return from_env

    if env is None:
        return None
    try:
        value = str(env.GetProjectOption("custom_cfgdocs_profile") or "").strip().lower()
    except Exception:
        return None
    if value in ("flowio", "waveshare", "generic"):
        return value
    return None


def _to_int(value: Any) -> Optional[int]:
    try:
        return int(value)
    except Exception:
        return None


def _env_flag(name: str, default: bool = False) -> bool:
    raw = os.getenv(name)
    if raw is None:
        return default
    return raw.strip().lower() not in ("", "0", "false", "off", "no")


def _apply_profile_specific_io_enum_sets(meta: dict, profile: str, tft_enabled: bool = False) -> dict:
    if not isinstance(meta, dict):
        return meta
    enum_sets = meta.get("enum_sets")
    if not isinstance(enum_sets, dict):
        return meta

    def sanitize_enum_entry(entry: dict, label: str) -> dict:
        out = dict(entry or {})
        out["label"] = label
        # Keep this label stable regardless of cfgdoc i18n token overlays.
        out.pop("label_t", None)
        out.pop("label_i18n", None)
        return out

    def non_connected_entry() -> dict:
        return {"value": 0, "label": "Non connecté"}

    def binding_entries_with_non_connected(entries: List[dict]) -> List[dict]:
        filtered = []
        for entry in entries:
            value = _to_int(entry.get("value"))
            if value is None or value == 0 or value == 65535:
                continue
            filtered.append(dict(entry))
        return [non_connected_entry()] + filtered

    # Analog bindings: Micronova's local DS18B20 GPIO binding is not valid on
    # flow.io boards, which expose DS18B20 probes through profile ports 120/121.
    analog_key = "flowio_binding_port_analog"
    analog_entries = enum_sets.get(analog_key)
    if profile in ("flowio", "waveshare") and isinstance(analog_entries, list):
        analog_filtered = [
            dict(entry)
            for entry in analog_entries
            if isinstance(entry, dict) and _to_int(entry.get("value")) != 2
        ]
        if profile == "waveshare":
            analog_labels_waveshare = {
                100: "ADSInt0 - ADS1115 interne canal 0 [100]",
                101: "ADSInt1 - ADS1115 interne canal 1 [101]",
                102: "ADSInt2 - ADS1115 interne canal 2 [102]",
                103: "ADSInt3 - ADS1115 interne canal 3 [103]",
                110: "ADSExt0 - ADS1115 externe paire diff 0 [110]",
                111: "ADSExt1 - ADS1115 externe paire diff 1 [111]",
                120: "OneWireWater - DS18B20 GPIO20 [120]",
                121: "OneWireAir - DS18B20 GPIO19 [121]",
                130: "SHT40Temp - SHT40 canal 0 [130]",
                131: "SHT40Humidity - SHT40 canal 1 [131]",
                132: "BMP280Temp - BMP280 canal 0 [132]",
                133: "BMP280Pressure - BMP280 canal 1 [133]",
                134: "BME688Temp - BME688 canal 0 [134]",
                135: "BME688Humidity - BME688 canal 1 [135]",
                136: "BME688Pressure - BME688 canal 2 [136]",
                137: "BME688Gas - BME688 canal 3 [137]",
                138: "INA226ShuntMv - INA226 canal 0 [138]",
                139: "INA226BusV - INA226 canal 1 [139]",
                140: "INA226CurrentMa - INA226 canal 2 [140]",
                141: "INA226PowerMw - INA226 canal 3 [141]",
                142: "INA226LoadV - INA226 canal 4 [142]",
            }
            analog_filtered = [
                sanitize_enum_entry(entry, analog_labels_waveshare[value])
                for entry in analog_filtered
                for value in [_to_int(entry.get("value"))]
                if value in analog_labels_waveshare
            ]
        enum_sets[analog_key] = binding_entries_with_non_connected(analog_filtered)

    # Digital input bindings: pin labels differ across flow.io and Waveshare.
    din_key = "flowio_binding_port_digital_input"
    din_entries = enum_sets.get(din_key)
    if isinstance(din_entries, list):
        current = [item for item in din_entries if isinstance(item, dict)]
        din_labels_flowio = {
            200: "DIN0 - GPIO34 [200]",
            201: "DIN1 - GPIO36 [201]",
            202: "DIN2 - GPIO39 [202]",
            203: "DIN3 - GPIO35 [203]",
        }
        din_labels_waveshare = {
            200: "GPIO04 - ESP32-S3 input [200]",
            201: "GPIO05 - ESP32-S3 input [201]",
            202: "GPIO06 - ESP32-S3 input [202]",
            203: "GPIO07 - ESP32-S3 input [203]",
            204: "GPIO08 - ESP32-S3 input [204]",
            205: "GPIO09 - ESP32-S3 input [205]",
            206: "GPIO10 - ESP32-S3 input [206]",
            207: "GPIO11 - ESP32-S3 input [207]",
            220: "GPA0 - MCP23017 input [220]",
            221: "GPA1 - MCP23017 input, unassigned [221]",
            222: "GPA2 - MCP23017 input, unassigned [222]",
            223: "GPA3 - MCP23017 input [223]",
            224: "GPA4 - MCP23017 input [224]",
            225: "GPA5 - MCP23017 input [225]",
            226: "GPA6 - MCP23017 input [226]",
            240: "GPIO1 - ESP32-S3 input [240]",
            241: "GPIO2 - ESP32-S3 input [241]",
            242: "GPIO21 - ESP32-S3 input [242]",
            243: "GPIO45 - ESP32-S3 input [243]",
            244: "GPIO47 - ESP32-S3 input [244]",
            245: "GPIO48 - ESP32-S3 input [245]",
        }
        if tft_enabled:
            for reserved_port in (240, 241, 242, 243, 244, 245):
                din_labels_waveshare.pop(reserved_port, None)

        selected_labels = None
        if profile == "flowio":
            selected_labels = din_labels_flowio
        elif profile == "waveshare":
            selected_labels = din_labels_waveshare

        if selected_labels is not None:
            filtered: List[dict] = []
            present_values = set()
            for entry in current:
                value = _to_int(entry.get("value"))
                if value is None or value not in selected_labels:
                    continue
                filtered.append(sanitize_enum_entry(entry, selected_labels[value]))
                present_values.add(value)
            for value, label in selected_labels.items():
                if value not in present_values:
                    filtered.append({"value": value, "label": label})
            enum_sets[din_key] = binding_entries_with_non_connected(filtered)

    # Digital output bindings: flow.io uses its onboard PCF8574; Waveshare exposes
    # its primary TCA9554/MCP23017 plus optional PCF8574 and TCA9554 instances.
    dout_key = "flowio_binding_port_digital_output"
    dout_entries = enum_sets.get(dout_key)
    if isinstance(dout_entries, list):
        current = [item for item in dout_entries if isinstance(item, dict)]
        filtered: List[dict] = []
        for entry in current:
            value = _to_int(entry.get("value"))
            if value is None:
                continue
            keep = True
            # Micronova aux_output must not be exposed on flow.io / flow.io profiles.
            if profile in ("flowio", "waveshare") and value == 1:
                keep = False
            if profile == "flowio":
                keep = keep and not (300 <= value <= 399)
            if keep:
                filtered.append(dict(entry))

        if profile == "waveshare":
            dout_labels_waveshare = {
                300: "EXIO1 - TCA9554 bit 0 [300]",
                301: "EXIO2 - TCA9554 bit 1 [301]",
                302: "EXIO3 - TCA9554 bit 2 [302]",
                303: "EXIO4 - TCA9554 bit 3 [303]",
                304: "EXIO5 - TCA9554 bit 4 [304]",
                305: "EXIO6 - TCA9554 bit 5 [305]",
                306: "EXIO7 - TCA9554 bit 6 [306]",
                307: "EXIO8 - TCA9554 bit 7 [307]",
                320: "GPB0 - MCP23017 output [320]",
                321: "GPB1 - MCP23017 output [321]",
                322: "GPB2 - MCP23017 output [322]",
                323: "GPB3 - MCP23017 output [323]",
                324: "GPB4 - MCP23017 output [324]",
                325: "GPB5 - MCP23017 output [325]",
                326: "GPB6 - MCP23017 output [326]",
                327: "GPB7 - MCP23017 output [327]",
                340: "GPIO1 - ESP32-S3 output [340]",
                341: "GPIO2 - ESP32-S3 output [341]",
                342: "GPIO21 - ESP32-S3 output [342]",
                343: "GPIO45 - ESP32-S3 output [343]",
                344: "GPIO47 - ESP32-S3 output [344]",
                345: "GPIO48 - ESP32-S3 output [345]",
            }
            if tft_enabled:
                for reserved_port in (340, 341, 342, 343, 344, 345):
                    dout_labels_waveshare.pop(reserved_port, None)
            relabeled: List[dict] = []
            present_values = set()
            for entry in filtered:
                value = _to_int(entry.get("value"))
                if value is not None and value in dout_labels_waveshare:
                    relabeled.append(sanitize_enum_entry(entry, dout_labels_waveshare[value]))
                    present_values.add(value)
                else:
                    if value is not None and value in dout_labels_waveshare:
                        relabeled.append(dict(entry))
            for value, label in dout_labels_waveshare.items():
                if value not in present_values:
                    relabeled.append({"value": value, "label": label})
            filtered = relabeled

        # Ensure flow.io exposes all 8 PCF bits (400..407) in UI bindings.
        if profile == "flowio":
            present_values = {_to_int(item.get("value")) for item in filtered}
            if 407 not in present_values:
                filtered.append(
                    {
                        "value": 407,
                        "label": "PortPCF0Bit7 - Sortie PCF8574 - Bit 7 [407]",
                    }
                )
        enum_sets[dout_key] = binding_entries_with_non_connected(filtered)

    # PoolLogic device slots: keep generic labels by default, but expose
    # profile wiring-specific mapping in UI for faster setup.
    slot_key = "poollogic_device_slot"
    slot_entries = enum_sets.get(slot_key)
    if profile == "waveshare" and isinstance(slot_entries, list):
        current = [item for item in slot_entries if isinstance(item, dict)]
        slot_labels_waveshare = {slot: f"pd{slot} -> d{slot:02d} [{slot}]" for slot in range(8)}
        current_by_value: Dict[int, dict] = {}
        for entry in current:
            value = _to_int(entry.get("value"))
            if value is not None:
                current_by_value[value] = entry
        relabeled: List[dict] = []
        for value in range(8):
            entry = current_by_value.get(value, {"value": value})
            relabeled.append(sanitize_enum_entry(entry, slot_labels_waveshare[value]))
        enum_sets[slot_key] = relabeled

    return meta


def _resolved_docs(docs: Dict[str, dict], translations: Dict[str, str]) -> Dict[str, dict]:
    return {
        key: _resolve_doc_i18n_fields(value, translations)
        for key, value in docs.items()
        if isinstance(value, dict)
    }


def _write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    project_dir = _get_project_dir()
    src_root = project_dir / "src"
    locale = os.getenv("FLOW_CFGDOC_LOCALE", "fr").strip().lower() or "fr"

    out_path = project_dir / "data" / "webinterface" / "cfgdocs.json"
    cfgmods_out_path = project_dir / "data" / "webinterface" / "cfgmods.json"

    cfgdocs_docs, cfgdocs_meta, cfgdocs_files = _load_text_docs(src_root, stem="cfgdocs", locale=locale)
    cfgmods_docs, cfgmods_meta, cfgmods_files = _load_text_docs(src_root, stem="cfgmods", locale=locale)
    i18n, i18n_files = _load_text_translations(src_root, locale=locale)

    pio_env = _detect_pio_env()
    profile = _profile_override_from_project_options() or _profile_from_pio_env(pio_env)
    tft_enabled = _env_flag("FLOW_CFGDOC_TFT_ENABLED")

    if profile == "waveshare":
        # The Waveshare runtime exposes GPIO04..GPIO11 plus five MCP23017 inputs.
        _expand_digital_input_slot_docs(cfgdocs_docs, 12)
        _expand_digital_input_slot_docs(cfgmods_docs, 12)
        _expand_digital_input_slot_translations(i18n, 12)
        _prune_io_slot_docs(cfgdocs_docs, analog_last=15, digital_last=12, output_last=15)
        _prune_io_slot_docs(cfgmods_docs, analog_last=15, digital_last=12, output_last=15)
        _prune_pool_device_docs(cfgdocs_docs, last_slot=7)
        _prune_pool_device_docs(cfgmods_docs, last_slot=7)

    combined_meta = _resolve_meta_i18n(_merge_meta_dict(cfgdocs_meta, cfgmods_meta), i18n)
    if profile == "waveshare":
        combined_meta = _prune_io_slot_meta(combined_meta, analog_last=15, digital_last=12, output_last=15)
    combined_meta = _apply_profile_specific_io_enum_sets(combined_meta, profile, tft_enabled)

    merged_docs = _resolved_docs(dict(cfgdocs_docs), i18n)

    cfgdocs_payload = {
        "_meta": {
            "generated": True,
            "locale": locale,
            "source": "text",
            "total": len(merged_docs),
        },
        "meta": combined_meta if isinstance(combined_meta, dict) else {},
        "docs": dict(sorted(merged_docs.items(), key=lambda kv: kv[0])),
    }
    _write_json(out_path, cfgdocs_payload)

    cfgmods_payload = {
        "_meta": {
            "generated": True,
            "locale": locale,
            "version": 1,
            "source": "text",
        },
        "meta": combined_meta if isinstance(combined_meta, dict) else {},
        "docs": dict(sorted(_resolved_docs(cfgmods_docs, i18n).items(), key=lambda kv: kv[0])),
    }
    _write_json(cfgmods_out_path, cfgmods_payload)

    print(
        f"[generate_config_docs] wrote {out_path} "
        f"(docs={len(cfgdocs_payload['docs'])} cfgmods={len(cfgmods_payload['docs'])} "
        f"text_files={len(cfgdocs_files) + len(cfgmods_files)} i18n_files={len(i18n_files)} "
        f"pio_env={pio_env or '-'} profile={profile} tft={int(tft_enabled)})"
    )


if __name__ == "__main__":
    main()
