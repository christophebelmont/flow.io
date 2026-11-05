from pathlib import Path
import gzip
import shutil
import os
import re
import subprocess

Import("env")


def _gzip_file(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    with src.open("rb") as in_file, gzip.GzipFile(filename="", mode="wb", fileobj=dst.open("wb"), mtime=0) as out_file:
        shutil.copyfileobj(in_file, out_file)


project_dir = Path(env.subst("$PROJECT_DIR"))
build_dir = Path(env.subst("$BUILD_DIR"))
src_dir = project_dir / "data"
staging_dir = build_dir / "spiffs_data"
pio_env = str(env.subst("$PIOENV") or "").strip()
cfgdocs_profile = ""
try:
    cfgdocs_profile = str(env.GetProjectOption("custom_cfgdocs_profile") or "").strip().lower()
except Exception:
    cfgdocs_profile = ""


def _cpp_define_enabled(name):
    for define in env.get("CPPDEFINES", []):
        if isinstance(define, (tuple, list)) and define:
            if str(define[0]) != name:
                continue
            value = define[1] if len(define) > 1 else 1
            return str(value).strip().lower() not in ("", "0", "false", "off")
        token = str(define).strip()
        if token == name:
            return True
        if token.startswith(name + "="):
            return token.split("=", 1)[1].strip().lower() not in ("", "0", "false", "off")

    try:
        raw_flags = env.GetProjectOption("build_flags")
    except Exception:
        raw_flags = ""
    if isinstance(raw_flags, (tuple, list)):
        raw_flags = " ".join(str(flag) for flag in raw_flags)
    match = re.search(r"(?:^|\s)-D\s*" + re.escape(name) + r"(?:=([^\s]+))?", str(raw_flags))
    if match:
        value = match.group(1) if match.group(1) is not None else "1"
        return value.strip().lower() not in ("", "0", "false", "off")
    return False


cfgdocs_tft_enabled = _cpp_define_enabled("FLOW_ENABLE_TFT_S3")


def _run_step(cmd):
    print(f"[prepare_spiffs_data] run: {' '.join(cmd)}")
    step_env = os.environ.copy()
    if pio_env:
        step_env["PIOENV"] = pio_env
    if cfgdocs_profile:
        step_env["FLOW_CFGDOC_PROFILE"] = cfgdocs_profile
    step_env["FLOW_CFGDOC_TFT_ENABLED"] = "1" if cfgdocs_tft_enabled else "0"
    subprocess.run(cmd, cwd=str(project_dir), check=True, env=step_env)

if src_dir.exists():
    transients = (
        project_dir / "data" / "webinterface" / "cfgdocs.json",
        project_dir / "data" / "webinterface" / "cfgmods.json",
        project_dir / "data" / "webinterface" / "cfgdocs.jz",
        project_dir / "data" / "webinterface" / "cfgmods.jz",
    )
    # Ensure a clean state before regeneration.
    for transient in transients:
        if transient.exists():
            transient.unlink()
    _run_step(["python3", "scripts/generate_config_docs.py"])
    _run_step(["python3", "scripts/generate_cfgdoc_chunks.py"])
    # Keep only segmented cfgdoc assets in source data.
    for transient in transients:
        if transient.exists():
            transient.unlink()
            print(f"[prepare_spiffs_data] removed transient {transient}")

    if staging_dir.exists():
        shutil.rmtree(staging_dir)
    staging_dir.mkdir(parents=True, exist_ok=True)

    compressed_sources = {
        Path("webinterface/index.html"): Path("webinterface/index.html.gz"),
        Path("webinterface/sh.html"): Path("webinterface/sh.html.gz"),
        Path("webinterface/app.js"): Path("webinterface/app.js.gz"),
        Path("webinterface/i18n/fr.json"): Path("webinterface/i18n/fr.json.gz"),
        Path("webinterface/i18n/en.json"): Path("webinterface/i18n/en.json.gz"),
        Path("webinterface/app-core.css"): Path("webinterface/app-core.css.gz"),
        Path("webinterface/app-core.js"): Path("webinterface/app-core.js.gz"),
        Path("webinterface/light.html"): Path("webinterface/light.html.gz"),
        Path("webinterface/light.css"): Path("webinterface/light.css.gz"),
        Path("webinterface/light.js"): Path("webinterface/light.js.gz"),
        Path("webinterface/prov.html"): Path("webinterface/prov.html.gz"),
        Path("webinterface/prov.js"): Path("webinterface/prov.js.gz"),
        Path("webinterface/runtimeui.json"): Path("webinterface/runtimeui.json.gz"),
    }
    cfgdoc_dir = src_dir / "wc"
    if cfgdoc_dir.exists():
        for cfgdoc_src in sorted(cfgdoc_dir.glob("*.j")):
            rel = cfgdoc_src.relative_to(src_dir)
            compressed_sources[rel] = rel.with_suffix(".j.gz")
    generated_outputs = set(compressed_sources.values())

    for path in src_dir.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(src_dir)
        if rel.parts[:2] == ("webinterface", "cfgdoc"):
            continue
        if rel in compressed_sources or rel in generated_outputs:
            continue
        dst = staging_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dst)

    for src_rel, dst_rel in compressed_sources.items():
        src = src_dir / src_rel
        if src.exists():
            _gzip_file(src, staging_dir / dst_rel)

    env.Replace(PROJECT_DATA_DIR=str(staging_dir), PROJECTDATA_DIR=str(staging_dir))
    print(f"[prepare_spiffs_data] staging {src_dir} -> {staging_dir}")
