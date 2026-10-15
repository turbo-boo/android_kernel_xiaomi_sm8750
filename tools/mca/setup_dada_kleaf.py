#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Create a pinned Dada Kleaf workspace from public MiCode/AOSP sources."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
from pathlib import Path
import subprocess

SOURCE = Path(__file__).resolve().parents[2]
LOCK = Path(__file__).with_name("dada_kleaf_sources.json")


def git(directory, *arguments):
    return subprocess.check_output(
        ["git", "-C", str(directory), *arguments], text=True).strip()


def check_project(path, revision):
    if git(path, "rev-parse", "HEAD") != revision:
        raise RuntimeError(f"Revision mismatch at {path}; existing checkout retained")


def clone_project(root, project, verify_only):
    destination = root / project["path"]
    if destination.exists():
        check_project(destination, project["sha"])
        return project["path"]
    if verify_only:
        raise RuntimeError(f"Missing checkout: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.mkdir()
    log_path = root / "setup-logs" / (project["path"].replace("/", "_") + ".log")
    log_path.parent.mkdir(exist_ok=True)
    with log_path.open("w") as log:
        commands = [
            ["git", "init", str(destination)],
            ["git", "-C", str(destination), "remote", "add", "origin", project["url"]],
            ["git", "-C", str(destination), "fetch", "--depth", "1", "origin", project["sha"]],
            ["git", "-C", str(destination), "checkout", "--detach", "FETCH_HEAD"],
        ]
        for command in commands:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=900)
            if result.returncode:
                raise RuntimeError(f"Checkout failed; see {log_path}")
    check_project(destination, project["sha"])
    return project["path"]


def link(root, path, target, verify_only):
    destination = root / path
    if destination.is_symlink():
        if destination.readlink().as_posix() != target:
            raise RuntimeError(f"Unexpected existing symlink: {destination}")
        return
    if destination.exists() or verify_only:
        raise RuntimeError(f"Missing or conflicting symlink: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.symlink_to(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument("--clone-jobs", type=int, default=4)
    parser.add_argument("--make-jobs", type=int, default=4)
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--build", action="store_true")
    args = parser.parse_args()
    if args.clone_jobs < 1 or args.make_jobs < 1:
        parser.error("Job counts must be positive")
    root = args.workspace.resolve()
    if root == SOURCE or root.is_relative_to(SOURCE):
        parser.error("Workspace must be outside the source checkout")
    root.mkdir(parents=True, exist_ok=True)
    source_revision = git(SOURCE, "rev-parse", "HEAD")
    kernel = root / "msm-kernel"
    if kernel.exists():
        check_project(kernel, source_revision)
    elif args.verify_only:
        raise RuntimeError(f"Missing source worktree: {kernel}")
    else:
        if git(SOURCE, "status", "--porcelain"):
            raise RuntimeError("Commit source changes before creating a new worktree")
        subprocess.run(["git", "-C", str(SOURCE), "worktree", "add", "--detach",
                        str(kernel), source_revision], check=True)
    lock = json.loads(LOCK.read_text())
    errors = []
    with ThreadPoolExecutor(max_workers=args.clone_jobs) as pool:
        futures = {pool.submit(clone_project, root, project, args.verify_only): project
                   for project in lock["projects"]}
        for future in as_completed(futures):
            try:
                print("Checked " + future.result(), flush=True)
            except Exception as error:
                errors.append(str(error))
    if errors:
        raise SystemExit("\n".join(errors))
    for path, target in {
        "tools/bazel": "../build/kernel/kleaf/bazel.sh",
        "WORKSPACE": "build/kernel/kleaf/bazel.WORKSPACE",
        ".bazelrc": "build/kernel/kleaf/common.bazelrc",
        "build/msm_kernel_extensions.bzl": "../msm-kernel/msm_kernel_extensions.bzl",
    }.items():
        link(root, path, target, args.verify_only)
    build_file = root / "build/BUILD.bazel"
    contents = 'package(default_visibility = ["//visibility:public"])\n'
    if build_file.exists():
        if build_file.read_text() != contents:
            raise RuntimeError(f"Unexpected existing file: {build_file}")
    elif args.verify_only:
        raise RuntimeError(f"Missing file: {build_file}")
    else:
        build_file.write_text(contents)
    for project in ["common", "msm-kernel"]:
        constants = (root / project / "build.config.constants").read_text()
        if "CLANG_VERSION=" + lock["clang"] not in constants:
            raise RuntimeError(f"Clang version mismatch in {project}")
        if "RUSTC_VERSION=" + lock["rust"] not in constants:
            raise RuntimeError(f"Rust version mismatch in {project}")
        common_config = (root / project / "build.config.common").read_text()
        if "KMI_GENERATION=" + str(lock["kmi_generation"]) not in common_config:
            raise RuntimeError(f"KMI generation mismatch in {project}")
    for header in ["common/mca_log.h", "common/mca_sysfs.h"]:
        if not (kernel / "include/linux/mca" / header).is_file():
            raise RuntimeError(f"Legacy MCA header path does not resolve: {header}")
    command = ["bash", "tools/bazel", "--batch", "--host_jvm_args=-Xmx1024m",
               "--host_jvm_args=-XX:ActiveProcessorCount=4", "build",
               "//msm-kernel:dada_perf", "--enable_bzlmod=false", "--config=local",
               "--define=FACTORY_BUILD=0", f"--make_jobs={args.make_jobs}",
               "--jobs=4", "--local_resources=cpu=4", "--local_resources=memory=4096"]
    print(f"Workspace checked: {len(lock['projects'])} pinned external projects")
    print("Build command: " + " ".join(command))
    if args.build:
        subprocess.run(command, cwd=root, check=True)


if __name__ == "__main__":
    main()
