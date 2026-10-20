"""Clean up ExportPS2Functions.java output so it can be used and committed.

Run after every export (Ghidra open with GhidraMCPPlugin enabled):
  python -I tools/postprocess_ghidra_export.py

- Rewrites the absolute paths in config/ghidra_export.toml to repo-relative ones and
  sends recompiler output to generated/ (the script's default, config/output, would be committed).
- Writes config/ghidra_functions_only.csv: the export minus Ghidra's ~23k code labels
  (entry_*, caseD_*, ...). PS2Recomp turns every label into its own entry point, which tripled
  the generated code (77 MB -> 286 MB) and doubled the unresolved-JR warnings, while the
  recompiler already derives ~31k internal entry points itself. If a runtime "function not found"
  ever points at a label address, switch ghidra_output back to config/ghidra_functions.csv.
"""
import csv
import re
import sys

sys.path.insert(0, "tools")
from ghidra_apply_sdk_names import ghidra_functions  # noqa: E402

TOML = "config/ghidra_export.toml"
FULL_CSV = "config/ghidra_functions.csv"
FUNCS_CSV = "config/ghidra_functions_only.csv"


def main():
    starts = set(ghidra_functions("http://127.0.0.1:8080/"))
    with open(FULL_CSV, newline="") as f:
        rows = list(csv.reader(f))
    keep = [rows[0]] + [r for r in rows[1:] if int(r[1], 16) in starts]
    with open(FUNCS_CSV, "w", newline="") as f:
        csv.writer(f).writerows(keep)
    print(f"{FUNCS_CSV}: kept {len(keep) - 1} function rows of {len(rows) - 1} (Ghidra has {len(starts)} functions)")

    with open(TOML) as f:
        text = f.read()
    for key, value in (("input", "game/disc/SCES_508.78"), ("output", "generated/"), ("ghidra_output", FUNCS_CSV)):
        text = re.sub(rf"^{key} = .*$", f'{key} = "{value}"', text, count=1, flags=re.M)
    with open(TOML, "w") as f:
        f.write(text)
    print(f"{TOML}: input/output/ghidra_output set to repo-relative paths")


if __name__ == "__main__":
    main()
