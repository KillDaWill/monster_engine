#!/usr/bin/env python3
"""Valida escenarios ecológicos y repetibilidad, sin contexto gráfico."""
import argparse
import csv
import json
from pathlib import Path
import subprocess


def run(executable, output, scenario, seed, days, suffix=""):
    name = scenario + suffix
    command = [str(executable), f"--scenario={scenario}", f"--seed={seed}",
               f"--days={days}", f"--summary={output / (name + '.json')}",
               f"--history={output / (name + '.csv')}"]
    with (output / (name + '.log')).open("w") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    result = json.loads((output / (name + '.json')).read_text())
    rows = list(csv.DictReader((output / (name + '.csv')).open()))
    assert all(0 <= int(row["population"]) <= 256 and float(row["plant_biomass"]) >= 0
               for row in rows), f"{name}: historia fuera de límites"
    assert result["deaths"] == sum(result["death_causes"]), f"{name}: causas incoherentes"
    assert result["kills"] == result["death_causes"][3], f"{name}: muertes duplicadas"
    assert abs(result["food_consumed"] - result["plant_consumed"] - result["meat_consumed"]) < 1e-6
    initial = {"A": 12, "B": 8, "C": 18, "D": 4, "E": 14}[scenario]
    assert result["population"] == initial + result["births"] - result["deaths"]
    return result, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=Path("demos/demo_ecosystem_headless"))
    parser.add_argument("--output", type=Path, default=Path("build/ecosystem-validation"))
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--days", type=float, default=30)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    executable = args.executable.resolve()
    results = {}
    for scenario in "ABCDE":
        data, command = run(executable, args.output, scenario, args.seed, args.days)
        if scenario == "A":
            assert data["death_causes"][1] > 0 and data["plant_consumed"] > 0
        elif scenario == "B":
            assert data["births"] > 0 and data["maximum_generation"] >= 2
        elif scenario == "C":
            assert data["kills"] > 0 and data["meat_consumed"] > 0 and data["escapes"] > 0
        elif scenario == "D":
            assert data["kills"] == 0 and data["food_consumed"] == 0 and data["death_causes"][1] > 0
        else:
            assert data["births"] > 0 and data["deaths"] > 0 and data["maximum_generation"] >= 2
        results[scenario] = {"passed": True, "command": command, "summary": data}
        print(f"{scenario}: PASS population={data['population']} births={data['births']} "
              f"deaths={data['deaths']} kills={data['kills']} generation={data['maximum_generation']}", flush=True)
    replay, _ = run(executable, args.output, "E", args.seed, args.days, "-replay")
    assert replay == results["E"]["summary"], "resumen no determinista"
    assert (args.output / "E.csv").read_bytes() == (args.output / "E-replay.csv").read_bytes(), "historia no determinista"
    results["determinism"] = {"passed": True}
    (args.output / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
    print("determinism: PASS", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
