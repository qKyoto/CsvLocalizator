from __future__ import annotations

import csv
import shutil
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

try:
    import unreal
except Exception:
    unreal = None

try:
    import polib
except ImportError as exc:
    raise ImportError(
        "polib is required. Enable PythonScriptPlugin and let CsvLocalizator install "
        "PythonRequirements, or vendor polib into Content/Python/Lib/site-packages."
    ) from exc


META_COLUMNS = ["id", "namespace", "key", "source"]


@dataclass(frozen=True)
class LocalizationEntry:
    entry_id: str
    namespace: str
    key: str
    source: str


def _log(message: str) -> None:
    if unreal:
        unreal.log(f"[CsvLocalizator] {message}")
    else:
        print(f"[CsvLocalizator] {message}")


def _warn(message: str) -> None:
    if unreal:
        unreal.log_warning(f"[CsvLocalizator] {message}")
    else:
        print(f"[CsvLocalizator][WARNING] {message}")


def _project_localization_root() -> Path:
    if unreal:
        return Path(unreal.Paths.project_content_dir()) / "Localization"
    return Path.cwd() / "Content" / "Localization"


def _normalize_path(path: Optional[str], fallback: Optional[Path] = None) -> Path:
    if path:
        return Path(path).expanduser().resolve()
    if fallback:
        return fallback.resolve()
    return _project_localization_root().resolve()


def _po_path(localization_root: Path, target: str, culture: str) -> Path:
    return localization_root / target / culture / f"{target}.po"


def find_localization_targets(localization_root: Optional[str] = None) -> Dict[str, List[str]]:
    root = _normalize_path(localization_root, _project_localization_root())
    result: Dict[str, List[str]] = {}

    if not root.exists():
        return result

    for target_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        cultures: List[str] = []
        for culture_dir in sorted(p for p in target_dir.iterdir() if p.is_dir()):
            po_file = culture_dir / f"{target_dir.name}.po"
            if po_file.exists():
                cultures.append(culture_dir.name)
        if cultures:
            result[target_dir.name] = cultures

    return result


def _load_po(path: Path):
    if not path.exists():
        raise FileNotFoundError(f"PO file not found: {path}")
    return polib.pofile(str(path), encoding="utf-8")


def _extract_key(entry) -> str:
    comments = []
    if getattr(entry, "comment", ""):
        comments.extend(str(entry.comment).splitlines())
    if getattr(entry, "tcomment", ""):
        comments.extend(str(entry.tcomment).splitlines())

    for line in comments:
        stripped = line.strip()
        if stripped.startswith("Key:"):
            return stripped.split("Key:", 1)[1].strip()
    return ""


def _make_entry_id(namespace: str, key: str, source: str) -> str:
    if namespace and key:
        return f"{namespace}::{key}"
    if namespace:
        return f"{namespace}::{source}"
    if key:
        return key
    return source


def _entry_info(entry) -> LocalizationEntry:
    namespace = entry.msgctxt or ""
    key = _extract_key(entry)
    source = entry.msgid or ""
    return LocalizationEntry(
        entry_id=_make_entry_id(namespace, key, source),
        namespace=namespace,
        key=key,
        source=source,
    )


def _is_translatable(entry) -> bool:
    return not entry.obsolete and bool(entry.msgid)


def _read_culture_entries(localization_root: Path, target: str, culture: str) -> Tuple[List[LocalizationEntry], Dict[str, str]]:
    po = _load_po(_po_path(localization_root, target, culture))
    ordered_entries: List[LocalizationEntry] = []
    translations: Dict[str, str] = {}

    for entry in po:
        if not _is_translatable(entry):
            continue

        info = _entry_info(entry)
        ordered_entries.append(info)
        translations[info.entry_id] = entry.msgstr or ""

    return ordered_entries, translations


def _select_cultures(all_cultures: Sequence[str], native_culture: str, mode: str, selected_culture: Optional[str]) -> List[str]:
    cultures = list(dict.fromkeys(all_cultures))

    if native_culture in cultures:
        cultures.remove(native_culture)
    cultures.insert(0, native_culture)

    if mode == "all":
        return cultures

    if not selected_culture:
        raise ValueError("selected_culture is required when mode='selected'")

    if selected_culture == native_culture:
        return [native_culture]

    if selected_culture not in cultures:
        raise ValueError(f"Culture '{selected_culture}' was not found in target cultures: {cultures}")

    return [native_culture, selected_culture]


def export_to_csv(
    localization_root: Optional[str] = None,
    target: str = "Game",
    native_culture: str = "en",
    mode: str = "all",
    selected_culture: str = "",
    output_path: Optional[str] = None,
) -> str:
    root = _normalize_path(localization_root, _project_localization_root())
    output = _normalize_path(output_path, Path.cwd() / "LocalizationCSV")
    targets = find_localization_targets(str(root))

    if target not in targets:
        raise ValueError(f"Localization target '{target}' not found under {root}")

    export_cultures = _select_cultures(targets[target], native_culture, mode, selected_culture or None)
    native_entries, native_translations = _read_culture_entries(root, target, native_culture)

    culture_translations: Dict[str, Dict[str, str]] = {native_culture: native_translations}
    for culture in export_cultures:
        if culture == native_culture:
            continue
        _, culture_translations[culture] = _read_culture_entries(root, target, culture)

    output.mkdir(parents=True, exist_ok=True)
    suffix = "all" if mode == "all" else selected_culture
    csv_path = output / f"{target}_{suffix}.csv"

    with csv_path.open("w", encoding="utf-8-sig", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=META_COLUMNS + export_cultures)
        writer.writeheader()

        for info in native_entries:
            row = {
                "id": info.entry_id,
                "namespace": info.namespace,
                "key": info.key,
                "source": info.source,
            }

            for culture in export_cultures:
                value = culture_translations.get(culture, {}).get(info.entry_id, "")
                row[culture] = value or (info.source if culture == native_culture else "")

            writer.writerow(row)

    _log(f"Exported {len(native_entries)} entries to {csv_path}")
    return str(csv_path)


def _read_csv(csv_path: Path) -> Tuple[List[str], List[Dict[str, str]]]:
    if not csv_path.exists():
        raise FileNotFoundError(f"CSV file not found: {csv_path}")

    with csv_path.open("r", encoding="utf-8-sig", newline="") as file:
        reader = csv.DictReader(file)
        if not reader.fieldnames:
            raise ValueError("CSV has no header")
        return list(reader.fieldnames), [dict(row) for row in reader]


def _culture_columns(fieldnames: Iterable[str]) -> List[str]:
    return [column for column in fieldnames if column not in META_COLUMNS and column.strip()]


def _index_po_entries(po) -> Dict[str, object]:
    entries: Dict[str, object] = {}
    for entry in po:
        if not _is_translatable(entry):
            continue
        entries[_entry_info(entry).entry_id] = entry
    return entries


def _clone_po_structure(source_po):
    new_po = polib.POFile()
    new_po.metadata = dict(source_po.metadata)
    return new_po


def _create_entry_from_row(row: Dict[str, str], translation: str):
    entry = polib.POEntry(
        msgctxt=row.get("namespace", "") or None,
        msgid=row.get("source", "") or "",
        msgstr=translation or "",
    )
    key = row.get("key", "")
    if key:
        entry.comment = f"Key: {key}"
    return entry


def _backup_file(path: Path) -> Optional[Path]:
    if not path.exists():
        return None

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = path.with_suffix(path.suffix + f".bak_{stamp}")
    shutil.copy2(path, backup)
    return backup


def import_from_csv(
    localization_root: Optional[str] = None,
    target: str = "Game",
    native_culture: str = "en",
    csv_path: Optional[str] = None,
) -> List[str]:
    root = _normalize_path(localization_root, _project_localization_root())
    if not csv_path:
        raise ValueError("csv_path is required")

    csv_file = _normalize_path(csv_path)
    fieldnames, rows = _read_csv(csv_file)
    cultures = _culture_columns(fieldnames)

    if not cultures:
        raise ValueError("CSV has no culture columns")
    if not rows:
        raise ValueError("CSV contains no rows")
    if native_culture not in cultures:
        _warn(f"Native culture '{native_culture}' was not found in CSV columns. Importing available cultures: {cultures}")

    base_culture = native_culture if native_culture in cultures else cultures[0]
    base_po_path = _po_path(root, target, base_culture)
    base_po = _load_po(base_po_path) if base_po_path.exists() else polib.POFile()

    written_paths: List[str] = []

    for culture in cultures:
        culture_po_path = _po_path(root, target, culture)
        culture_po_path.parent.mkdir(parents=True, exist_ok=True)

        po = _load_po(culture_po_path) if culture_po_path.exists() else _clone_po_structure(base_po)
        entries_by_id = _index_po_entries(po)

        changed_count = 0
        for row in rows:
            entry_id = row.get("id", "")
            if not entry_id:
                continue

            translation = row.get(culture, "") or ""
            entry = entries_by_id.get(entry_id)
            if entry is None:
                entry = _create_entry_from_row(row, translation)
                po.append(entry)
                entries_by_id[entry_id] = entry
                changed_count += 1
                continue

            if entry.msgstr != translation:
                entry.msgstr = translation
                changed_count += 1

        backup = _backup_file(culture_po_path)
        po.save(str(culture_po_path))
        written_paths.append(str(culture_po_path))

        backup_note = f", backup: {backup}" if backup else ""
        _log(f"Updated {culture_po_path} ({changed_count} changed{backup_note})")

    _log("Import finished. Run Localization Dashboard Compile if compiled localization resources are needed.")
    return written_paths
