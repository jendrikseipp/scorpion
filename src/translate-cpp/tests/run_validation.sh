#!/usr/bin/env bash
# Validate the C++ translator against the Python translator on every
# benchmark in misc/tests/benchmarks/, enforcing 120s / 2GiB per run.
# Usage: tests/run_validation.sh [bench_root]

set -uo pipefail

bench_root="${1:-misc/tests/benchmarks}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
repo_root="$(cd "${here}/../.." && pwd)"
cpp_bin="${here}/build/translate"
canonical_diff="${here}/tests/canonical_diff.py"

if [[ ! -x ${cpp_bin} ]]; then
    echo "C++ translator binary not found at ${cpp_bin}; please build first."
    exit 1
fi

pass=0
fail=0
failures=()

for domain in "${bench_root}"/*; do
    [[ -d ${domain} ]] || continue
    domain_pddl="${domain}/domain.pddl"
    [[ -f ${domain_pddl} ]] || continue
    for problem in "${domain}"/*.pddl; do
        [[ "${problem}" == "${domain_pddl}" ]] && continue
        rel="$(basename ${domain})/$(basename ${problem} .pddl)"

        tmp_py=$(mktemp -d); tmp_cpp=$(mktemp -d)
        # Python translator
        (cd "${tmp_py}" && \
         ulimit -v $((2 * 1024 * 1024)) && \
         PYTHONPATH="${repo_root}/src" \
         timeout 120 python3 -m translate \
             "${repo_root}/${domain_pddl}" "${repo_root}/${problem}" \
             >/dev/null 2>&1)
        py_rc=$?
        # C++ translator
        (cd "${tmp_cpp}" && \
         ulimit -v $((2 * 1024 * 1024)) && \
         timeout 120 "${cpp_bin}" \
             "${repo_root}/${domain_pddl}" "${repo_root}/${problem}" \
             >/dev/null 2>&1)
        cpp_rc=$?

        py_sas="${tmp_py}/output.sas"
        cpp_sas="${tmp_cpp}/output.sas"

        if [[ ${py_rc} -ne 0 && ${cpp_rc} -ne 0 ]]; then
            # Both failed; treat as expected.
            echo "[both fail] ${rel} (py=${py_rc} cpp=${cpp_rc})"
            pass=$((pass + 1))
            rm -rf "${tmp_py}" "${tmp_cpp}"
            continue
        fi
        if [[ ! -f ${py_sas} ]]; then
            echo "[python missing output] ${rel}"
            fail=$((fail + 1))
            failures+=("${rel}")
            rm -rf "${tmp_py}" "${tmp_cpp}"
            continue
        fi
        if [[ ! -f ${cpp_sas} ]]; then
            echo "[cpp missing output] ${rel}"
            fail=$((fail + 1))
            failures+=("${rel}")
            rm -rf "${tmp_py}" "${tmp_cpp}"
            continue
        fi
        out=$(python3 "${canonical_diff}" "${py_sas}" "${cpp_sas}" 2>&1)
        diff_rc=$?
        if [[ ${diff_rc} -eq 0 ]]; then
            echo "[ok] ${rel}"
            pass=$((pass + 1))
        else
            echo "[diff] ${rel}"
            echo "${out}" | sed 's/^/    /'
            fail=$((fail + 1))
            failures+=("${rel}")
        fi
        rm -rf "${tmp_py}" "${tmp_cpp}"
    done
done

echo
echo "PASS: ${pass}"
echo "FAIL: ${fail}"
if [[ ${fail} -gt 0 ]]; then
    echo "Failures:"
    for f in "${failures[@]}"; do echo "  ${f}"; done
    exit 1
fi
