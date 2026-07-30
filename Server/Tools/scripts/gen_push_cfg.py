"""
gen_push_cfg.py — 从 dist/assets 自动生成 h2PushPromise.cfg

用法：
    python gen_push_cfg.py [config.json]

配置文件格式见 gen_push_cfg.json：
    dist_dir: 前端构建产物目录（相对于本脚本或绝对路径）
    output:   输出 cfg 路径（相对于本脚本或绝对路径）
    rules:    路由 -> push 资源前缀列表
              前缀格式: "main.css" 表示匹配 assets/main-*.css
"""

import json
import sys
import os
import glob
from datetime import datetime
from pathlib import Path


def resolve_path(base: Path, p: str) -> Path:
    path = Path(p)
    if path.is_absolute():
        return path
    return (base / path).resolve()


def find_asset(assets_dir: Path, prefix_spec: str) -> str | None:
    name, ext = prefix_spec.rsplit(".", 1)
    pattern = f"{name}-*.{ext}"
    matches = sorted(assets_dir.glob(pattern))
    if matches:
        return matches[0].name
    exact = assets_dir / f"{name}.{ext}"
    if exact.exists():
        return exact.name
    return None


def generate_cfg(rules: list, assets_dir: Path) -> tuple[str, list[str]]:
    errors = []
    blocks = []

    for rule in rules:
        route = rule["route"]
        pushes = []
        for spec in rule["push"]:
            filename = find_asset(assets_dir, spec)
            if filename:
                pushes.append(f"    /assets/{filename}")
            else:
                errors.append(f"路由 '{route}': 未找到匹配 '{spec}' 的资源")
        if pushes:
            body = ",\n".join(pushes)
            blocks.append(f"{route} {{\n{body}\n}}")

    header = (
        f"# h2PushPromise.cfg — 自动生成于 {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n"
        f"# 由 gen_push_cfg.py 生成，勿手动编辑\n"
        f"# 配置驱动: gen_push_cfg.json\n\n"
    )
    return header + "\n\n".join(blocks) + "\n", errors


def main():
    script_dir = Path(__file__).parent
    config_path = Path(sys.argv[1]) if len(sys.argv) > 1 else script_dir / "gen_push_cfg.json"

    if not config_path.exists():
        print(f"错误: 配置文件不存在: {config_path}", file=sys.stderr)
        sys.exit(1)

    with open(config_path, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    base_dir = config_path.parent
    assets_dir = resolve_path(base_dir, cfg["dist_dir"]) / "assets"
    output_path = resolve_path(base_dir, cfg["output"])

    if not assets_dir.is_dir():
        print(f"错误: assets 目录不存在: {assets_dir}", file=sys.stderr)
        print("请先执行 npm run build", file=sys.stderr)
        sys.exit(1)

    content, errors = generate_cfg(cfg["rules"], assets_dir)

    if errors:
        for e in errors:
            print(f"警告: {e}", file=sys.stderr)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")
    print(f"已生成: {output_path}")
    print(content)

    if errors:
        sys.exit(2)


if __name__ == "__main__":
    main()
