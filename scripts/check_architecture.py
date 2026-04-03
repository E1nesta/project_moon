#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path


INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"', re.MULTILINE)

EXPECTED_MODULE_SHAPES = {
    "login": {"application", "domain", "infrastructure"},
    "player": {"application", "domain", "infrastructure", "ports"},
    "dungeon": {"application", "domain", "infrastructure", "ports"},
}

EXPECTED_RUNTIME_DIRS = {
    "foundation",
    "observability",
    "protocol",
    "execution",
    "transport",
    "storage",
    "session",
}


def rel(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def read_includes(path: Path) -> list[str]:
    try:
        return INCLUDE_RE.findall(path.read_text(encoding="utf-8"))
    except UnicodeDecodeError:
        return []


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def module_name(parts: tuple[str, ...]) -> str | None:
    if len(parts) >= 2 and parts[0] == "modules":
        return parts[1]
    return None


def main() -> int:
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    errors: list[str] = []

    modules_dir = root / "modules"
    runtime_dir = root / "runtime"
    apps_dir = root / "apps"

    actual_runtime_dirs = {path.name for path in runtime_dir.iterdir() if path.is_dir()}
    if actual_runtime_dirs != EXPECTED_RUNTIME_DIRS:
        fail(errors, f"runtime 目录应为 {sorted(EXPECTED_RUNTIME_DIRS)}，当前是 {sorted(actual_runtime_dirs)}")

    for mod_name, expected_dirs in EXPECTED_MODULE_SHAPES.items():
        mod_dir = modules_dir / mod_name
        actual_dirs = {path.name for path in mod_dir.iterdir() if path.is_dir()}
        if actual_dirs != expected_dirs:
            fail(errors, f"modules/{mod_name} 应为 {sorted(expected_dirs)}，当前是 {sorted(actual_dirs)}")

    if (modules_dir / "login" / "ports").exists():
        fail(errors, "modules/login 不应再保留 ports 目录")

    source_files = [
        path
        for base in (apps_dir, modules_dir, runtime_dir)
        for path in base.rglob("*")
        if path.suffix in {".h", ".cpp"}
    ]

    for path in source_files:
        relative = rel(path, root)
        parts = path.relative_to(root).parts
        includes = read_includes(path)

        for include in includes:
            if include.startswith("game_backend.pb.h"):
                continue

            include_parts = tuple(Path(include).parts)
            owner = module_name(parts)
            included_owner = module_name(include_parts)

            if parts[0] == "runtime":
                if include.startswith("modules/") or include.startswith("apps/"):
                    fail(errors, f"{relative}: runtime 不应依赖业务或 app 代码 -> {include}")
                continue

            if parts[0] == "modules" and owner is not None:
                layer = parts[2]

                if layer == "domain":
                    if include.startswith("runtime/"):
                        fail(errors, f"{relative}: domain 不应依赖 runtime -> {include}")
                    if included_owner is not None and included_owner != owner:
                        fail(errors, f"{relative}: domain 不应依赖其他模块 -> {include}")
                    continue

                if layer == "application":
                    if include.startswith(f"modules/{owner}/infrastructure/"):
                        fail(errors, f"{relative}: application 不应依赖本模块 infrastructure -> {include}")
                    if included_owner is not None and included_owner != owner:
                        fail(errors, f"{relative}: application 不应依赖其他模块 -> {include}")
                    if owner == "login":
                        if include.startswith("runtime/") and not (
                            include.startswith("runtime/foundation/") or include.startswith("runtime/session/")
                        ):
                            fail(errors, f"{relative}: login application 只应依赖 runtime/foundation 或 runtime/session -> {include}")
                    elif include.startswith("runtime/") and not include.startswith("runtime/foundation/"):
                        fail(errors, f"{relative}: application 只应依赖 runtime/foundation -> {include}")
                    continue

                if layer == "infrastructure":
                    if included_owner is not None and included_owner != owner:
                        fail(errors, f"{relative}: infrastructure 不应依赖其他模块 -> {include}")
                    continue

            if parts[0] == "apps":
                app_name = parts[1]
                if app_name == "gateway":
                    if include.startswith("modules/"):
                        fail(errors, f"{relative}: gateway app 不应直接依赖业务模块 -> {include}")
                    continue

                owned_module = app_name
                allowed_prefixes = {
                    f"modules/{owned_module}/",
                    "runtime/",
                    f"apps/{app_name}/",
                }

                if app_name == "dungeon":
                    allowed_prefixes.add("modules/player/infrastructure/")
                    allowed_prefixes.add("modules/player/ports/")

                if include.startswith("modules/") and not any(include.startswith(prefix) for prefix in allowed_prefixes):
                    fail(errors, f"{relative}: app 依赖越界 -> {include}")

    if errors:
        print("architecture guard failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("architecture guard passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
