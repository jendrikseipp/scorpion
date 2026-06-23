#!/usr/bin/env python3
"""
Check or update reference output files for translate and preprocess.

This script runs the Fast Downward translator and preprocessor on a set of test
PDDL files and either:
- Updates the reference SAS files (--update mode)
- Checks outputs against existing references (--check mode)

These references are used by the CI system to detect unintended changes to the
translator/preprocessor output.
"""

import argparse
import difflib
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path


# Repository root directory
REPO_ROOT = Path(__file__).parent.parent.parent
FAST_DOWNWARD = REPO_ROOT / "fast-downward.py"
REFERENCE_DIR = REPO_ROOT / "misc" / "tests" / "translate-preprocess-references"
TRANSLATE_REF_DIR = REFERENCE_DIR / "translate"
PREPROCESS_REF_DIR = REFERENCE_DIR / "preprocess"
BENCHMARK_DIR = REPO_ROOT / "misc" / "tests" / "benchmarks"

# Maximum file size to store as reference.
MAX_REFERENCE_FILE_SIZE = 100 * 1024  # 100 KiB in bytes


def auto_detect_test_problems():
    """Auto-detect all problem files in the benchmarks directory."""
    test_problems = []

    # Iterate through all subdirectories in benchmarks
    for domain_dir in sorted(BENCHMARK_DIR.iterdir()):
        if not domain_dir.is_dir():
            continue

        # Find all PDDL files in this directory
        pddl_files = sorted(domain_dir.glob("*.pddl"))

        problem_files = [f for f in pddl_files if "domain" not in f.name.lower()]

        # Add each problem file
        for problem_file in problem_files:
            # Create a name from domain folder and problem file
            problem_name = f"{domain_dir.name}-{problem_file.stem}"
            # Convert to relative path from repo root
            problem_path = str(problem_file.relative_to(REPO_ROOT))
            test_problems.append((problem_path, None, problem_name))

    return test_problems


# Test problems to process
# Each entry is (problem_file, optional_domain_file, name)
TEST_PROBLEMS = auto_detect_test_problems()


def run_command(cmd, cwd=None, verbose=False):
    """Run a command and return the result."""
    if verbose:
        print(f"Running: {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(
        cmd,
        cwd=cwd,
        capture_output=True,
        text=True
    )
    return result


def compute_file_hash(file_path):
    """Compute SHA256 hash of a file."""
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()


def compare_files(file1, file2, show_diff=False):
    """Compare two files and return True if they match."""
    if not file1.exists():
        print(f"ERROR: File not found: {file1}")
        return False
    if not file2.exists():
        print(f"ERROR: Reference file not found: {file2}")
        return False

    hash1 = compute_file_hash(file1)
    hash2 = compute_file_hash(file2)

    if hash1 == hash2:
        return True

    if show_diff:
        with open(file1) as f1, open(file2) as f2:
            diff = difflib.unified_diff(
                f2.readlines(),
                f1.readlines(),
                fromfile=str(file2),
                tofile=str(file1),
                lineterm=''
            )
            print("\nDifference found:")
            for line in diff:
                print(line)

    return False


def run_fast_downward(problem_file, domain_file, preprocess, name):
    """Run Fast Downward translator (and optionally preprocessor).

    Returns (success, output_file) tuple where:
    - success: bool indicating if the run succeeded
    - output_file: Path to the output file if successful, None otherwise
    """
    os.chdir(REPO_ROOT)

    # Determine output file and command based on stage
    if preprocess:
        output_file = REPO_ROOT / "preprocessed-output.sas"
        stage_args = ["--translate", "--preprocess"]
        stage_name = "Preprocessing"
    else:
        output_file = REPO_ROOT / "output.sas"
        stage_args = ["--translate"]
        stage_name = "Translation"

    # Clean up old output file
    if output_file.exists():
        output_file.unlink()

    # Build command
    cmd = [sys.executable, str(FAST_DOWNWARD)] + stage_args + ["--keep-sas-file"]
    if domain_file:
        cmd.append(str(REPO_ROOT / domain_file))
    cmd.append(str(REPO_ROOT / problem_file))

    # Run command
    result = run_command(cmd)

    if result.returncode != 0:
        print(f"ERROR: {stage_name} failed for {name}")
        print(f"STDOUT:\n{result.stdout}")
        print(f"STDERR:\n{result.stderr}")
        return False, None

    if not output_file.exists():
        print(f"ERROR: {output_file.name} not created for {name}")
        return False, None

    return True, output_file


def cleanup_output_files(preprocess):
    """Clean up output files after processing."""
    if preprocess:
        preprocessed_sas = REPO_ROOT / "preprocessed-output.sas"
        if preprocessed_sas.exists():
            preprocessed_sas.unlink()

    output_sas = REPO_ROOT / "output.sas"
    if output_sas.exists():
        output_sas.unlink()


def generate_reference(problem_file, domain_file, name, preprocess):
    """Generate reference output for translation or preprocessing.

    Args:
        preprocess: If True, generate preprocess reference. If False, translate reference.
    """
    stage_name = "preprocess" if preprocess else "translate"
    ref_dir = PREPROCESS_REF_DIR if preprocess else TRANSLATE_REF_DIR

    print(f"\n{'='*60}")
    print(f"Generating {stage_name} reference for: {name}")
    print(f"{'='*60}")

    # Run Fast Downward
    success, output_file = run_fast_downward(problem_file, domain_file, preprocess, name)
    if not success:
        return False

    # Check file size
    file_size = output_file.stat().st_size
    if file_size >= MAX_REFERENCE_FILE_SIZE:
        print(f"SKIPPED: File too large ({file_size / 1024:.2f} KiB, limit: {MAX_REFERENCE_FILE_SIZE / 1024:.2f} KiB)")
        print(f"Reference will not be stored for {name}")
        # Remove existing reference file if it exists
        ref_file = ref_dir / f"{name}.sas"
        if ref_file.exists():
            ref_file.unlink()
            print(f"Removed existing reference file: {ref_file}")
        cleanup_output_files(preprocess)
        return True

    # Copy to reference directory
    ref_file = ref_dir / f"{name}.sas"
    shutil.copy(output_file, ref_file)

    file_hash = compute_file_hash(ref_file)
    print(f"SUCCESS: Created {ref_file} ({file_size / 1024:.1f} KiB)")
    print(f"File hash: {file_hash}")

    cleanup_output_files(preprocess)
    return True


def check_output(problem_file, domain_file, name, preprocess, show_diff=False):
    """Check translate or preprocess output against reference.

    Args:
        preprocess: If True, check preprocess output. If False, check translate output.
    """
    stage_name = "preprocess" if preprocess else "translate"
    ref_dir = PREPROCESS_REF_DIR if preprocess else TRANSLATE_REF_DIR

    # Check if reference exists first
    ref_file = ref_dir / f"{name}.sas"
    if not ref_file.exists():
        print(f"\nSkipping {name}: No {stage_name} reference file")
        return True

    print(f"\nChecking {stage_name} output for: {name}")

    # Run Fast Downward
    success, output_file = run_fast_downward(problem_file, domain_file, preprocess, name)
    if not success:
        return False

    # If reference doesn't exist, check if the current output is too large
    if not ref_file.exists():
        file_size = output_file.stat().st_size
        if file_size >= MAX_REFERENCE_FILE_SIZE:
            print(f"✓ No reference (file too large: {file_size / 1024:.2f} KiB)")
        else:
            print(f"ERROR: Reference file not found: {ref_file}")
            cleanup_output_files(preprocess)
            return False
        cleanup_output_files(preprocess)
        return True

    # Compare with reference
    matches = compare_files(output_file, ref_file, show_diff)

    if matches:
        print(f"✓ {stage_name.capitalize()} output matches reference")
    else:
        print(f"✗ {stage_name.capitalize()} output differs from reference!")
        print(f"  Current: {compute_file_hash(output_file)}")
        print(f"  Reference: {compute_file_hash(ref_file)}")

    cleanup_output_files(preprocess)
    return matches


def generate_translate_reference(problem_file, domain_file, name):
    """Generate reference output for translation."""
    return generate_reference(problem_file, domain_file, name, preprocess=False)


def check_translate_output(problem_file, domain_file, name, show_diff=False):
    """Check translate output against reference."""
    return check_output(problem_file, domain_file, name, preprocess=False, show_diff=show_diff)


def generate_preprocess_reference(problem_file, domain_file, name):
    """Generate reference output for preprocessing."""
    return generate_reference(problem_file, domain_file, name, preprocess=True)


def check_preprocess_output(problem_file, domain_file, name, show_diff=False):
    """Check preprocess output against reference."""
    return check_output(problem_file, domain_file, name, preprocess=True, show_diff=show_diff)


def main():
    parser = argparse.ArgumentParser(
        description="Check or update translate/preprocess reference outputs"
    )

    # Mode selection (mutually exclusive)
    mode_group = parser.add_mutually_exclusive_group(required=True)
    mode_group.add_argument(
        "--update",
        action="store_true",
        help="Update reference files with current outputs"
    )
    mode_group.add_argument(
        "--check",
        action="store_true",
        help="Check current outputs against reference files"
    )

    # Stage selection
    parser.add_argument(
        "--translate-only",
        action="store_true",
        help="Only process translate stage"
    )
    parser.add_argument(
        "--preprocess-only",
        action="store_true",
        help="Only process preprocess stage"
    )

    # Check mode options
    parser.add_argument(
        "--show-diff",
        action="store_true",
        help="Show detailed diffs for mismatches (check mode only)"
    )

    args = parser.parse_args()

    # Check if fast-downward.py exists
    if not FAST_DOWNWARD.exists():
        print(f"ERROR: {FAST_DOWNWARD} not found!")
        sys.exit(1)

    # For check mode, verify reference directories exist
    if args.check:
        if not args.preprocess_only and not TRANSLATE_REF_DIR.exists():
            print(f"ERROR: Translate reference directory not found: {TRANSLATE_REF_DIR}")
            print("Run this script with --update to generate references.")
            sys.exit(1)
        if not args.translate_only and not PREPROCESS_REF_DIR.exists():
            print(f"ERROR: Preprocess reference directory not found: {PREPROCESS_REF_DIR}")
            print("Run this script with --update to generate references.")
            sys.exit(1)

    # For update mode, ensure reference directories exist
    if args.update:
        TRANSLATE_REF_DIR.mkdir(parents=True, exist_ok=True)
        PREPROCESS_REF_DIR.mkdir(parents=True, exist_ok=True)

    print(f"Repository root: {REPO_ROOT}")
    print(f"Fast Downward script: {FAST_DOWNWARD}")
    if args.update:
        print("Mode: UPDATE")
        if not args.preprocess_only:
            print(f"Translate reference directory: {TRANSLATE_REF_DIR}")
        if not args.translate_only:
            print(f"Preprocess reference directory: {PREPROCESS_REF_DIR}")
    else:
        print("Mode: CHECK")

    translate_success = []
    translate_failures = []
    preprocess_success = []
    preprocess_failures = []

    # Process each test problem
    for problem_file, domain_file, name in TEST_PROBLEMS:
        if not args.preprocess_only:
            if args.update:
                success = generate_translate_reference(problem_file, domain_file, name)
            else:
                success = check_translate_output(problem_file, domain_file, name, args.show_diff)

            if success:
                translate_success.append(name)
            else:
                translate_failures.append(name)

        if not args.translate_only:
            if args.update:
                success = generate_preprocess_reference(problem_file, domain_file, name)
            else:
                success = check_preprocess_output(problem_file, domain_file, name, args.show_diff)

            if success:
                preprocess_success.append(name)
            else:
                preprocess_failures.append(name)

    # Print summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")

    if not args.preprocess_only:
        stage_name = "Translate references" if args.update else "Translate checks"
        print(f"\n{stage_name}:")
        if args.update:
            print(f"  Success: {len(translate_success)}")
            print(f"  Failures: {len(translate_failures)}")
        else:
            print(f"  Passed: {len(translate_success)}/{len(TEST_PROBLEMS)}")
            print(f"  Failed: {len(translate_failures)}/{len(TEST_PROBLEMS)}")
        if translate_failures:
            print(f"  Failed: {', '.join(translate_failures)}")

    if not args.translate_only:
        stage_name = "Preprocess references" if args.update else "Preprocess checks"
        print(f"\n{stage_name}:")
        if args.update:
            print(f"  Success: {len(preprocess_success)}")
            print(f"  Failures: {len(preprocess_failures)}")
        else:
            print(f"  Passed: {len(preprocess_success)}/{len(TEST_PROBLEMS)}")
            print(f"  Failed: {len(preprocess_failures)}/{len(TEST_PROBLEMS)}")
        if preprocess_failures:
            print(f"  Failed: {', '.join(preprocess_failures)}")

    # Exit with appropriate message and code
    if translate_failures or preprocess_failures:
        if args.check:
            print("\n" + "="*60)
            print("FAILURE: Some outputs differ from references!")
            print("="*60)
            print("\nIf these changes are intentional, update references with:")
            print(f"  python3 {Path(__file__).relative_to(REPO_ROOT)} --update")
        sys.exit(1)
    else:
        if args.check:
            print("\n" + "="*60)
            print("SUCCESS: All outputs match references!")
            print("="*60)


if __name__ == "__main__":
    main()
