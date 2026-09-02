#!/usr/bin/env bash
set -euo pipefail

if (($# > 1)) || { (($# == 1)) && [[ "$1" != "--self-test" ]]; }; then
  echo "usage: tools/check_nolint.sh [--self-test]" >&2
  exit 2
fi

if [[ "${1:-}" == "--self-test" ]]; then
  script=$(realpath "$0")
  test_root=$(mktemp -d)
  trap 'rm -rf "$test_root"' EXIT
  git -C "$test_root" init -q

  expect() {
    local outcome=$1
    local name=$2
    local content=$3
    printf '%s\n' "$content" >"$test_root/case.cpp"
    git -C "$test_root" add case.cpp

    if (cd "$test_root" && "$script" >/dev/null 2>&1); then
      actual=pass
    else
      actual=fail
    fi
    if [[ "$actual" != "$outcome" ]]; then
      echo "self-test failed: $name expected $outcome, got $actual" >&2
      return 1
    fi
  }

  expect pass direct 'call(); // NOLINT(bugprone-use-after-move) -- API consumes but does not move'
  expect pass nextline $'// NOLINTNEXTLINE(performance-move-const-arg) -- move documents ownership transfer\ncall();'
  expect pass block $'// NOLINTBEGIN(clang-analyzer-core.CallAndMessage) -- synthetic invalid state fixture\ncall();\n// NOLINTEND(clang-analyzer-core.CallAndMessage) -- end synthetic invalid state fixture'
  expect fail bare 'call(); // NOLINT'
  expect fail unnamed 'call(); // NOLINT() -- legacy waiver'
  expect fail unexplained 'call(); // NOLINT(bugprone-use-after-move)'
  expect fail wildcard 'call(); // NOLINT(bugprone-*) -- too broad'
  expect fail nested $'// NOLINTBEGIN(check-one) -- outer fixture\n// NOLINTBEGIN(check-two) -- nested fixture\n// NOLINTEND(check-two) -- nested fixture\n// NOLINTEND(check-one) -- outer fixture'
  expect fail mismatched $'// NOLINTBEGIN(check-one) -- fixture\n// NOLINTEND(check-two) -- fixture'
  expect fail unclosed '// NOLINTBEGIN(check-one) -- fixture'

  echo "clang-tidy suppression policy self-test: 10 cases passed"
  exit 0
fi

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

mapfile -t matches < <(
  git grep --untracked -n -I 'NOLINT' -- \
    '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' || true
)

# A suppression is an exceptional, local waiver. Require exact check names (no
# wildcard profiles) and make the reason reviewable on the same line. BEGIN/END
# pairs additionally have to be unnested and name the same checks.
directive_re='^NOLINT(BEGIN|END|NEXTLINE)?\(([[:alnum:]_.-]+([[:space:]]*,[[:space:]]*[[:alnum:]_.-]+)*)\)[[:space:]]+--[[:space:]]+[^[:space:]].*$'
parse_re='^NOLINT(BEGIN|END|NEXTLINE)?\(([^)]*)\)'
declare -A active_checks=()
declare -A active_lines=()
failed=0

for match in "${matches[@]}"; do
  file=${match%%:*}
  remainder=${match#*:}
  line=${remainder%%:*}
  source=${remainder#*:}
  directive=NOLINT${source#*NOLINT}

  if [[ ! "$directive" =~ $directive_re ]] ||
    [[ "${directive#NOLINT}" == *NOLINT* ]]; then
    echo "${file}:${line}: invalid clang-tidy suppression" >&2
    echo "  use NOLINT[BEGIN|END|NEXTLINE](exact-check[,exact-check]) -- justification" >&2
    failed=1
    continue
  fi

  [[ "$directive" =~ $parse_re ]]
  kind=${BASH_REMATCH[1]}
  checks=${BASH_REMATCH[2]//[[:space:]]/}

  case "$kind" in
    BEGIN)
      if [[ -n "${active_checks[$file]:-}" ]]; then
        echo "${file}:${line}: nested NOLINTBEGIN is not allowed" >&2
        failed=1
      else
        active_checks[$file]=$checks
        active_lines[$file]=$line
      fi
      ;;
    END)
      if [[ -z "${active_checks[$file]:-}" ]]; then
        echo "${file}:${line}: NOLINTEND has no matching NOLINTBEGIN" >&2
        failed=1
      elif [[ "${active_checks[$file]}" != "$checks" ]]; then
        echo "${file}:${line}: NOLINTEND checks do not match NOLINTBEGIN at line ${active_lines[$file]}" >&2
        failed=1
        unset 'active_checks[$file]' 'active_lines[$file]'
      else
        unset 'active_checks[$file]' 'active_lines[$file]'
      fi
      ;;
  esac
done

for file in "${!active_checks[@]}"; do
  echo "${file}:${active_lines[$file]}: NOLINTBEGIN has no matching NOLINTEND" >&2
  failed=1
done

if ((failed)); then
  exit 1
fi

echo "clang-tidy suppression policy: ${#matches[@]} directive(s) valid"
