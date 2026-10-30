#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors
"""Inspect existing FIN logs/diagnostics without rerunning a solver (Python 3.6+)."""

import argparse
from collections import Counter
import json
from pathlib import Path
import re


STATUS_NAMES = {
    0: "OPTIMAL", 1: "FEASIBLE", 2: "INFEASIBLE", 3: "UNBOUNDED",
    4: "ABNORMAL", 5: "MODEL_INVALID", 6: "NOT_SOLVED",
}
REASONS = {
    "tile_lower_bound_exceeds_upper_bound": "tile의 fill 최소 면적이 최대 면적을 초과함",
    "tile_lower_sum_exceeds_window_upper_bound": "tile 최소 fill 합이 window 상한을 초과함",
    "tile_upper_sum_below_window_lower_bound": "tile 최대 fill 합이 window 최소 요구량보다 작음",
    "window_lower_bound_exceeds_upper_bound": "window의 최소 요구량이 상한을 초과함",
    "window_lower_bound_exceeds_legal_capacity": "window의 최소 요구량이 legal fill 용량을 초과함",
}


def read_json(path):
    try:
        value = json.loads(path.read_text())
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        return {}


def diagnostic_files(directory):
    found = []
    for path in sorted(directory.glob("*.json")):
        value = read_json(path)
        if "solver_status" in value and "diagnosis" in value:
            found.append({"file": str(path), "data": value})
    return found


def inspect_log(path):
    content = path.read_text(errors="replace")
    diagnostics = diagnostic_files(path.parent)
    result = read_json(path.parent.parent / "result.json")
    # A case's result.json refers to its latest attempt only.
    recorded_log = result.get("log")
    if not recorded_log or Path(recorded_log).resolve() != path:
        result = {}
    end = re.findall(r"\[Fill LP\].*solve end.*status=(\d+)", content)
    codes = [int(code) for code in end]
    states = [STATUS_NAMES.get(code, "UNKNOWN_%d" % code) for code in codes]
    errors = re.findall(r"\[ERROR [^\]]+\].*", content)
    returncode = result.get("returncode")
    if "is not a known keyword or flag" in content:
        outcome = "OPTION_ERROR"
    elif result.get("status") == "timeout" or returncode == 124:
        outcome = "TIMEOUT"
    elif returncode in (139, -11) or "Segmentation fault" in content:
        outcome = "CRASH"
    elif 2 in codes or any(d["data"].get("solver_status") == "INFEASIBLE"
                           for d in diagnostics):
        outcome = "INFEASIBLE"
    elif errors or any(code not in (0, 1) for code in codes):
        outcome = "FILL_OR_SOLVER_FAILED"
    elif returncode not in (None, 0):
        outcome = "PROCESS_FAILED"
    elif diagnostics:
        outcome = "PRECHECK_OR_SOLVER_FAILED"
    elif returncode == 0 or "pass" in content.splitlines():
        outcome = "COMPLETED"
    else:
        outcome = "RUNNING_OR_INCOMPLETE"
    return {
        "source": str(path), "outcome": outcome,
        "solver_statuses": states, "returncode": returncode,
        "runner_status": result.get("status"),
        "wall_seconds": result.get("wall_seconds"),
        "last_stage": ("solver_returned" if codes else "solver_running_or_interrupted"
                       if "[Fill LP]" in content and "solve start" in content
                       else "after_def_load_before_solver" if "Finished DEF file:" in content
                       else "before_solver"),
        "errors": errors, "diagnostics": diagnostics,
    }


def inspect_path(path):
    if not path.exists():
        raise ValueError("Path does not exist: " + str(path))
    if path.is_file():
        if path.suffix == ".json":
            value = read_json(path)
            if "solver_status" not in value or "diagnosis" not in value:
                raise ValueError("Expected a FIN LP failure diagnostic JSON: " + str(path))
            return [{"source": str(path), "outcome": value["solver_status"],
                     "solver_statuses": [value["solver_status"]], "errors": [],
                     "diagnostics": [{"file": str(path), "data": value}]}]
        return [inspect_log(path)]
    logs = sorted(path.rglob("openroad.log"))
    if logs:
        return [inspect_log(log.resolve()) for log in logs]
    found = []
    for metadata in sorted(path.rglob("*.json")):
        value = read_json(metadata)
        if "solver_status" in value and "diagnosis" in value:
            found.extend(inspect_path(metadata))
    return found


def print_record(record):
    print("\n[%s] %s" % (record["outcome"], record["source"]))
    print("  Solver 상태: %s" % (", ".join(record["solver_statuses"]) or "반환 기록 없음"))
    if record.get("returncode") is not None:
        print("  프로세스: exit=%s, wall=%ss, runner=%s" % (
            record["returncode"], record.get("wall_seconds"), record.get("runner_status")))
    if record.get("last_stage"):
        print("  마지막 확인 단계: " + record["last_stage"])
    for error in record["errors"]:
        print("  " + error)
    if record["outcome"] == "OPTION_ERROR":
        print("  옵션/실행 파일 버전 불일치입니다. LP infeasibility 결과가 아닙니다.")
    elif record["outcome"] == "TIMEOUT":
        print("  시간 제한으로 종료됐습니다. 이것만으로 LP가 infeasible하다고 판단할 수 없습니다.")
    elif record["outcome"] == "CRASH":
        print("  비정상 종료입니다. 충돌한 LP 제약이 확인됐다는 뜻은 아닙니다.")
    if not record["diagnostics"] and record["outcome"] == "INFEASIBLE":
        print("  정확한 충돌 위치: 미식별 (상세 진단 파일 없음). 이 로그만으로는 복원할 수 없습니다.")
    for diagnostic in record["diagnostics"]:
        data = diagnostic["data"]
        print("  진단 JSON: " + diagnostic["file"])
        print("  layer=%s, solver=%s, lp_threads=%s" % (
            data.get("layer"), data.get("solver"), data.get("lp_threads")))
        print("  window 최소 밀도: 요청=%s, 실제 LP 적용=%s" % (
            data.get("requested_min_window_density"), data.get("effective_min_window_density")))
        precheck = data.get("precheck_reason")
        if precheck:
            print("  사전 검사: " + REASONS.get(precheck, precheck))
        conflict = data.get("conflict")
        if conflict:
            print("  충돌: " + REASONS.get(conflict.get("reason"), str(conflict.get("reason"))))
            for key, value in conflict.items():
                if key != "reason":
                    print("    %s: %s" % (key, value))
            print("  단위: 좌표=%s, 면적=%s" % (
                data.get("coordinate_units"), data.get("area_units")))
        else:
            print("  정확한 충돌 위치: 미식별 (conflict=null).")
            print("  단순 한도 전파로 찾지 못했습니다. 제약이 만족된다는 뜻은 아닙니다.")
        diagnosis = data.get("diagnosis", {})
        print("  한도 전파: updates=%s, limited=%s, is_iis=%s" % (
            diagnosis.get("update_count"), diagnosis.get("limited"), diagnosis.get("is_iis")))
        for key in ("model_file", "constraints_file", "tiles_file", "windows_file", "bound_updates_file"):
            if data.get(key):
                print("  %s: %s" % (key, data[key]))
    if record.get("runner_status") == "ok" and record["outcome"] != "COMPLETED":
        print("  주의: 기존 runner의 ok 표시는 실제 solver/실패 로그와 다릅니다.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", type=Path, nargs="+", help="Experiment/attempt directory, log, or diagnostic JSON")
    parser.add_argument("--json", action="store_true", help="Print machine-readable results instead of text")
    args = parser.parse_args()
    records = []
    seen = set()
    try:
        for path in args.paths:
            for record in inspect_path(path.resolve()):
                if record["source"] not in seen:
                    records.append(record)
                    seen.add(record["source"])
    except (ValueError, OSError) as error:
        parser.error(str(error))
    if args.json:
        print(json.dumps(records, indent=2, ensure_ascii=False))
    else:
        for record in records:
            print_record(record)
        counts = Counter(record["outcome"] for record in records)
        print("\n실행 분류: " + (", ".join("%s=%d" % pair for pair in sorted(counts.items())) or "기록 없음"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
