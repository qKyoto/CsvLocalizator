try:
    import unreal_po_csv
    unreal_po_csv.find_localization_targets()
except Exception as exc:
    try:
        import unreal
        unreal.log_warning(f"[CsvLocalizator] Python package loaded, but startup scan failed: {exc}")
    except Exception:
        pass
