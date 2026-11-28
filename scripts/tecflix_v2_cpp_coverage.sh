#!/usr/bin/env bash
# Ejecutar después de configurar los tres builds con cobertura de Clang.
set -euo pipefail
if [[ $# != 3 ]]; then
    echo "Uso: bash $0 BUILD_USUARIOS BUILD_METADATA BUILD_STREAMING" >&2
    exit 2
fi
shopt -s nullglob
profdata=(llvm-profdata)
coverage=(llvm-cov)
if command -v xcrun >/dev/null; then
    profdata=(xcrun llvm-profdata)
    coverage=(xcrun llvm-cov)
fi
test_coverage() {
    local build="$1" profile_dir
    shift
    profile_dir=$(mktemp -d "${TMPDIR:-/tmp}/tecflix-profile.XXXXXX")
    LLVM_PROFILE_FILE="$profile_dir/%m-%p.profraw" ctest --test-dir "$build" --output-on-failure --parallel 4
    "${profdata[@]}" merge -sparse "$profile_dir"/*.profraw -o "$build/coverage.profdata"
    local primary="$1"
    shift
    local objects=() executable
    for executable in "$@"; do objects+=("-object=$build/$executable"); done
    "${coverage[@]}" report "$build/$primary" "${objects[@]}" -instr-profile="$build/coverage.profdata" \
        -ignore-filename-regex='(/_deps/|/third_party/|/tests/|/Applications/|/Library/Developer/|/opt/homebrew/|/usr/)' \
        | tee "$build/coverage.txt"
}
test_coverage "$1" user-service tests/user_service_tests
test_coverage "$2" metadata-service tests/metadata_service_tests
test_coverage "$3" tecflix-api streaming-auth-tests quota-client-tests upload-record-store-tests streaming-quota-integration-tests
