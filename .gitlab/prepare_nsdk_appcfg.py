#!/usr/bin/env python3

import argparse
import json
import re
import sys
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(
        description="Prepare a QEMU CI appcfg from the SDK application config."
    )
    parser.add_argument(
        "--input",
        required=True,
        help="Path to the source SDK application.json",
    )
    parser.add_argument(
        "--output",
        required=True,
        help="Path to write the generated CI application config",
    )
    parser.add_argument(
        "--group-config",
        help="Optional JSON file defining CI appdir groups",
    )
    parser.add_argument(
        "--group",
        help="Optional group name selected from --group-config",
    )
    parser.add_argument(
        "--selection-config",
        help="Optional JSON file containing appdirs and appdirs_ignore",
    )
    parser.add_argument(
        "--appdirs",
        help="Optional comma or whitespace separated appdirs override",
    )
    parser.add_argument(
        "--appdirs-ignore",
        help="Optional comma or whitespace separated appdirs_ignore override",
    )
    return parser.parse_args()


def normalize_path_list(value):
    if value is None:
        return None

    if isinstance(value, list):
        raw_items = value
    else:
        raw_items = re.split(r"[,\s]+", str(value).strip())

    return [str(item).strip() for item in raw_items if str(item).strip()]


def load_group_config(group_config_path, group_name):
    if not group_config_path:
        raise ValueError("--group-config is required when --group is specified")

    with Path(group_config_path).open("r", encoding="utf-8") as fp:
        config = json.load(fp)

    groups = config.get("groups", config)
    if group_name not in groups:
        raise ValueError(f"Missing SDK app group '{group_name}' in {group_config_path}")

    group = groups[group_name]
    if not isinstance(group, dict):
        raise ValueError(f"SDK app group '{group_name}' must be a JSON object")

    appdirs = normalize_path_list(group.get("appdirs", []))
    appdirs_ignore = normalize_path_list(group.get("appdirs_ignore", []))
    if not appdirs:
        raise ValueError(f"SDK app group '{group_name}' has no appdirs")

    return appdirs, appdirs_ignore


def load_selection_config(selection_config_path):
    with Path(selection_config_path).open("r", encoding="utf-8") as fp:
        config = json.load(fp)

    appdirs = normalize_path_list(config.get("appdirs", []))
    appdirs_ignore = normalize_path_list(config.get("appdirs_ignore", []))
    if not appdirs:
        raise ValueError(f"Selection config '{selection_config_path}' has no appdirs")

    return appdirs, appdirs_ignore


def main():
    args = parse_args()
    input_path = Path(args.input)
    output_path = Path(args.output)

    with input_path.open("r", encoding="utf-8") as fp:
        config = json.load(fp)

    appdirs = None
    appdirs_ignore = []
    if args.selection_config:
        try:
            appdirs, appdirs_ignore = load_selection_config(args.selection_config)
        except ValueError as exc:
            print(str(exc), file=sys.stderr)
            return 1
    elif args.group:
        try:
            appdirs, appdirs_ignore = load_group_config(args.group_config, args.group)
        except ValueError as exc:
            print(str(exc), file=sys.stderr)
            return 1

    appdirs_override = normalize_path_list(args.appdirs)
    if appdirs_override is not None:
        appdirs = appdirs_override

    appdirs_ignore_override = normalize_path_list(args.appdirs_ignore)
    if appdirs_ignore_override is not None:
        appdirs_ignore = appdirs_ignore_override

    if appdirs is not None:
        config["appdirs"] = appdirs
    config["appdirs_ignore"] = appdirs_ignore

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as fp:
        json.dump(config, fp, indent=4)
        fp.write("\n")

    print(f"Prepared CI appcfg: {output_path}")
    if args.group:
        print(f"Selected SDK app group: {args.group}")
    if args.selection_config:
        print(f"Selected SDK appdirs from: {args.selection_config}")
    print(f"appdirs: {config.get('appdirs', [])}")
    print(f"appdirs_ignore: {config.get('appdirs_ignore', [])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
