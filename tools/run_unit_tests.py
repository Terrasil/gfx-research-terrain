from __future__ import annotations

import importlib.util
import inspect
import sys
import traceback
from pathlib import Path


def load_module(path: Path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[path.stem] = module
    spec.loader.exec_module(module)
    return module


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    files = sorted((root / "tests").glob("test_*.py"))
    passed = 0
    failed = 0
    for path in files:
        module = load_module(path)
        for name, function in inspect.getmembers(module, inspect.isfunction):
            if not name.startswith("test_"):
                continue
            try:
                function()
                print(f"[PASS] {path.name}::{name}")
                passed += 1
            except Exception:
                print(f"[FAIL] {path.name}::{name}")
                traceback.print_exc()
                failed += 1
    print(f"unit/static tests: {passed} passed, {failed} failed")
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
