#!/usr/bin/env python3

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run SDK ThreadX regression matrix for QEMU CI."
    )
    parser.add_argument(
        "--config",
        required=True,
        help="Path to the ThreadX regression matrix JSON",
    )
    parser.add_argument(
        "--sdk-root",
        default=os.environ.get("NUCLEI_SDK_ROOT"),
        help="Path to the Nuclei SDK root, defaults to NUCLEI_SDK_ROOT",
    )
    parser.add_argument(
        "--download",
        default=os.environ.get("THREADX_DOWNLOAD", "ddr"),
        help="Default DOWNLOAD mode passed to the SDK ThreadX scripts",
    )
    parser.add_argument(
        "--parallel",
        default=os.environ.get("THREADX_PARALLEL", "4"),
        help="Default PARALLEL value passed to run_elfs.sh",
    )
    parser.add_argument(
        "--timeout",
        default=os.environ.get("THREADX_TIMEOUT", "60"),
        help="Default TIMEOUT value passed to run_elfs.sh",
    )
    parser.add_argument(
        "--log-root",
        default=os.environ.get("THREADX_LOG_ROOT"),
        help="Root directory for ThreadX CI logs",
    )
    parser.add_argument(
        "--cases",
        default=os.environ.get("THREADX_CASES"),
        help="Optional CASES override for the SDK ThreadX scripts",
    )
    parser.add_argument(
        "--case-list-file",
        default=os.environ.get("THREADX_CASE_LIST_FILE"),
        help="Optional CASE_LIST_FILE override for the SDK ThreadX scripts",
    )
    parser.add_argument(
        "--case-set",
        default=os.environ.get("THREADX_CASE_SET"),
        help="Optional CASE_SET override for the SDK ThreadX scripts",
    )
    parser.add_argument(
        "--suite",
        default=os.environ.get("THREADX_SUITE"),
        help="Optional suite filter, for example regression_up or regression_smp",
    )
    parser.add_argument(
        "--core",
        default=os.environ.get("THREADX_CORE"),
        help="Optional core filter, for example n900fd or nx900fd",
    )
    return parser.parse_args()


def load_config(config_path):
    with config_path.open("r", encoding="utf-8") as fp:
        config = json.load(fp)

    if isinstance(config, dict):
        matrix = config.get("threadx_regressions")
    else:
        matrix = config
        config = {}

    if not isinstance(matrix, list) or not matrix:
        raise ValueError(
            f"Invalid ThreadX matrix in {config_path}: expected a non-empty list"
        )

    return config, matrix


def split_filter(value):
    if not value:
        return None

    return {item.strip() for item in str(value).replace(",", " ").split() if item.strip()}


def filter_matrix(matrix, args):
    suites = split_filter(args.suite)
    cores = split_filter(args.core)
    if not suites and not cores:
        return matrix

    selected = []
    for entry in matrix:
        if suites and entry.get("suite") not in suites:
            continue
        if cores and entry.get("core") not in cores:
            continue
        selected.append(entry)

    if not selected:
        filters = []
        if suites:
            filters.append(f"suite={','.join(sorted(suites))}")
        if cores:
            filters.append(f"core={','.join(sorted(cores))}")
        raise ValueError("No ThreadX matrix entries matched " + " ".join(filters))

    return selected


def resolve_suite(sdk_root, suite_name, smp_value):
    if suite_name == "regression_up":
        return sdk_root / "test/threadx/regression_up", "threadx_regression_up.elf", ""
    if suite_name == "regression_smp":
        return (
            sdk_root / "test/threadx/regression_smp",
            "threadx_regression_smp.elf",
            str(smp_value or 4),
        )
    raise ValueError(f"Unsupported suite: {suite_name}")


def resolve_eclic(core_name, eclic_value, eclic_hwctx):
    eclic_value = str(eclic_value)
    eclic_hwctx = str(eclic_hwctx)

    if eclic_value == "1":
        return "eclicv1", "XLCFG_ECLIC=1"

    if eclic_value == "2":
        if eclic_hwctx not in {"0", "1"}:
            raise ValueError(
                f"THREADX_ECLIC_HWCTX must be 0 or 1 when THREADX_ECLIC=2, got {eclic_hwctx}"
            )
        return f"eclicv2-hwctx{eclic_hwctx}", f"XLCFG_ECLIC=2 ECLIC_HWCTX={eclic_hwctx}"

    raise ValueError(f"Unsupported eclic mode for {core_name}: {eclic_value}")


def set_optional_env(env, key, value):
    if value is None or value == "":
        env.pop(key, None)
    else:
        env[key] = str(value)


def normalize_case_name(case_name):
    case_name = str(case_name).strip()
    if case_name.startswith("./"):
        case_name = case_name[2:]
    if case_name.startswith("cases/"):
        case_name = case_name[len("cases/"):]
    if case_name.endswith(".c"):
        case_name = case_name[:-2]
    if case_name.endswith(".elf"):
        case_name = case_name[:-4]
    return case_name


def suite_case_names(suite_dir):
    return sorted(path.stem for path in (suite_dir / "cases").glob("*.c"))


def get_skip_cases(config, entry):
    suite_name = entry.get("suite", "")
    skip_cases = []
    global_skip_cases = config.get("skip_cases", {})

    if isinstance(global_skip_cases, dict):
        skip_cases.extend(global_skip_cases.get("*", []))
        skip_cases.extend(global_skip_cases.get(suite_name, []))
    elif isinstance(global_skip_cases, list):
        skip_cases.extend(global_skip_cases)

    skip_cases.extend(entry.get("skip_cases", []))
    return sorted({normalize_case_name(case_name) for case_name in skip_cases})


def resolve_case_filter(config, entry, suite_dir, job_root, args):
    if args.cases or args.case_list_file or args.case_set:
        return args.cases, args.case_list_file, args.case_set

    skip_cases = get_skip_cases(config, entry)
    if not skip_cases:
        return None, None, None

    all_cases = suite_case_names(suite_dir)
    missing_cases = [case_name for case_name in skip_cases if case_name not in all_cases]
    if missing_cases:
        raise ValueError(
            "Skip cases are not present in %s: %s"
            % (suite_dir, ", ".join(missing_cases))
        )

    selected_cases = [case_name for case_name in all_cases if case_name not in skip_cases]
    if not selected_cases:
        raise ValueError(f"All ThreadX cases were skipped for {suite_dir}")

    case_list_file = job_root / "selected_cases.txt"
    with case_list_file.open("w", encoding="utf-8") as fp:
        for case_name in selected_cases:
            fp.write(f"{case_name}\n")

    print(f"ThreadX skipped cases: {', '.join(skip_cases)}", flush=True)
    print(f"ThreadX selected cases: {len(selected_cases)} listed in {case_list_file}", flush=True)
    return " ".join(selected_cases), None, None


def read_summary_failures(summary_log):
    ignored_verdicts = {"[PASS]", "[SKIP]", "[BUILD-ONLY]"}
    failures = []

    if not summary_log.is_file():
        return failures

    with summary_log.open("r", encoding="utf-8", errors="replace") as fp:
        for line in fp:
            line = line.strip()
            if not line.startswith("["):
                continue

            fields = line.split(None, 1)
            if not fields or fields[0] in ignored_verdicts:
                continue

            failures.append(line)

    return failures


def print_summary_failures(summary_log, step_name, limit=30):
    failures = read_summary_failures(summary_log)
    if not failures:
        return

    print(
        f"ThreadX {step_name} failure details from {summary_log}:",
        file=sys.stderr,
        flush=True,
    )
    for failure in failures[:limit]:
        print(f"  {failure}", file=sys.stderr, flush=True)

    remaining = len(failures) - limit
    if remaining > 0:
        print(f"  ... {remaining} more", file=sys.stderr, flush=True)


def run_threadx_step(command, cwd, env, summary_log, step_name):
    try:
        subprocess.run(command, cwd=cwd, env=env, check=True)
    except subprocess.CalledProcessError:
        print_summary_failures(summary_log, step_name)
        raise


def build_env(
    base_env,
    core,
    smp,
    download,
    target_elf,
    elf_output_dir,
    log_dir,
    makeflags,
    cases,
    case_list_file,
    case_set,
):
    env = base_env.copy()
    env["CORE"] = core
    set_optional_env(env, "SMP", smp)
    env["DOWNLOAD"] = download
    env["TARGET_ELF"] = target_elf
    env["ELF_OUTPUT_DIR"] = str(elf_output_dir)
    env["LOG_DIR"] = str(log_dir)
    env["BUILD_ONLY"] = "1"
    existing_makeflags = env.get("MAKEFLAGS", "")
    env["MAKEFLAGS"] = f"{makeflags} {existing_makeflags}".strip()
    set_optional_env(env, "CASES", cases)
    set_optional_env(env, "CASE_LIST_FILE", case_list_file)
    set_optional_env(env, "CASE_SET", case_set)
    return env


def run_env(
    base_env,
    core,
    smp,
    download,
    elf_dir,
    log_dir,
    parallel,
    timeout,
    cases,
    case_list_file,
    case_set,
):
    env = base_env.copy()
    env["RUNNER"] = "qemu"
    env["CORE"] = core
    set_optional_env(env, "SMP", smp)
    env["DOWNLOAD"] = download
    env["ELF_DIR"] = str(elf_dir)
    env["LOG_DIR"] = str(log_dir)
    env["PARALLEL"] = str(parallel)
    env["TIMEOUT"] = str(timeout)
    set_optional_env(env, "CASES", cases)
    set_optional_env(env, "CASE_LIST_FILE", case_list_file)
    set_optional_env(env, "CASE_SET", case_set)
    return env


def run_one_entry(base_env, sdk_root, log_root, defaults, config, args, entry):
    suite_name = entry["suite"]
    core = entry["core"]
    smp = entry.get("smp", "")
    download = entry.get("download", defaults["download"])
    parallel = entry.get("parallel", defaults["parallel"])
    timeout = entry.get("timeout", defaults["timeout"])

    suite_dir, target_elf, smp = resolve_suite(sdk_root, suite_name, smp)
    if not suite_dir.is_dir():
        raise FileNotFoundError(f"Missing ThreadX suite directory: {suite_dir}")

    eclic_label, makeflags = resolve_eclic(
        core,
        entry["eclic"],
        entry.get("eclic_hwctx", 0),
    )
    matrix_name = entry.get("matrix_name", f"{core}-{eclic_label}")

    job_root = log_root / suite_name / matrix_name
    elf_dir = job_root / "elfs"
    build_log_dir = job_root / "build"
    run_log_dir = job_root / "run"
    elf_dir.mkdir(parents=True, exist_ok=True)
    build_log_dir.mkdir(parents=True, exist_ok=True)
    run_log_dir.mkdir(parents=True, exist_ok=True)
    cases, case_list_file, case_set = resolve_case_filter(
        config,
        entry,
        suite_dir,
        job_root,
        args,
    )

    print(f"ThreadX suite: {suite_name}", flush=True)
    print(f"ThreadX core: {core}", flush=True)
    print(f"ThreadX smp: {smp or 'n/a'}", flush=True)
    print(f"ThreadX eclic: {eclic_label}", flush=True)
    print(f"ThreadX make flags: {makeflags}", flush=True)
    print(f"ThreadX download: {download}", flush=True)
    print(f"ThreadX logs: {job_root}", flush=True)

    run_threadx_step(
        ["./test.sh"],
        cwd=suite_dir,
        env=build_env(
            base_env,
            core,
            smp,
            download,
            target_elf,
            elf_dir,
            build_log_dir,
            makeflags,
            cases,
            case_list_file,
            case_set,
        ),
        summary_log=build_log_dir / "summary.log",
        step_name=f"{suite_name}/{matrix_name} build",
    )
    run_threadx_step(
        ["./run_elfs.sh"],
        cwd=suite_dir,
        env=run_env(
            base_env,
            core,
            smp,
            download,
            elf_dir,
            run_log_dir,
            parallel,
            timeout,
            cases,
            case_list_file,
            case_set,
        ),
        summary_log=run_log_dir / "summary.log",
        step_name=f"{suite_name}/{matrix_name} run",
    )


def entry_label(entry):
    suite_name = entry.get("suite", "unknown")
    core = entry.get("core", "unknown")
    matrix_name = entry.get("matrix_name", f"{suite_name}:{core}")
    return f"{suite_name}/{matrix_name}"


def main():
    args = parse_args()
    if not args.sdk_root:
        print(
            "NUCLEI_SDK_ROOT is not set and --sdk-root was not provided",
            file=sys.stderr,
            flush=True,
        )
        return 1

    sdk_root = Path(args.sdk_root).resolve()
    config_path = Path(args.config).resolve()
    if not config_path.is_file():
        print(f"Missing ThreadX matrix config: {config_path}", file=sys.stderr, flush=True)
        return 1

    log_root = (
        Path(args.log_root).resolve()
        if args.log_root
        else sdk_root / "logs/nuclei_threadx_qemu_ci"
    )
    config, matrix = load_config(config_path)
    try:
        matrix = filter_matrix(matrix, args)
    except ValueError as exc:
        print(str(exc), file=sys.stderr, flush=True)
        return 1

    if args.suite or args.core:
        print(
            f"ThreadX matrix filters: suite={args.suite or '*'} core={args.core or '*'}",
            flush=True,
        )
    defaults = {
        "download": args.download,
        "parallel": args.parallel,
        "timeout": args.timeout,
    }

    failures = []
    base_env = os.environ.copy()

    for index, entry in enumerate(matrix, start=1):
        print(f"========== ThreadX regression {index}/{len(matrix)} ==========", flush=True)
        try:
            run_one_entry(base_env, sdk_root, log_root, defaults, config, args, entry)
        except (KeyError, ValueError, FileNotFoundError, subprocess.CalledProcessError) as exc:
            label = entry_label(entry)
            print(f"[THREADX-FAIL] {label}: {exc}", file=sys.stderr, flush=True)
            failures.append(label)

    if failures:
        print("ThreadX regression failures:", file=sys.stderr, flush=True)
        for label in failures:
            print(f"- {label}", file=sys.stderr, flush=True)
        return 1

    print(f"ThreadX regression matrix passed: {len(matrix)} entries", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
