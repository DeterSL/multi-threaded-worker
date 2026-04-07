#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path


DEFAULT_SCENARIOS = ["ycsbt_uni", "ycsbt_zipf", "dhr"]


def expand_scenarios(values: list[str]) -> set[str]:
    scenarios: set[str] = set()
    for value in values:
        for part in value.replace(",", " ").split():
            if part == "hotel-reservation":
                scenarios.add("dhr")
            elif part in {"hotel", "d_hotel_reservation"}:
                scenarios.add("dhr")
            elif part == "ycsb":
                scenarios.update({"ycsbt_uni", "ycsbt_zipf"})
            else:
                scenarios.add(part)
    return scenarios


def result_files(results_dir: Path) -> set[str]:
    if not results_dir.exists():
        return set()
    return {path.name for path in results_dir.iterdir() if path.is_file()}


def should_emit(file_name: str, existing_results: set[str], include_existing: bool) -> bool:
    return include_existing or file_name not in existing_results


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent
    default_output = script_dir / "detersl_experiments_config.csv"
    default_results_dir = repo_root / "results"

    parser = argparse.ArgumentParser(description="Generate DeterSL experiment config CSV")
    parser.add_argument("--partitions", type=int, required=True, help="Number of logical partitions passed to clients")
    parser.add_argument("--n_keys", type=int, required=True, help="Number of keys for YCSB-T")
    parser.add_argument("--experiment_time", type=int, required=True, help="Total experiment time in seconds")
    parser.add_argument("--warmup_time", type=int, required=True, help="Warmup time in seconds")
    parser.add_argument(
        "--scenarios",
        nargs="+",
        default=DEFAULT_SCENARIOS,
        help="Scenarios to generate: ycsbt_uni, ycsbt_zipf, dhr, ycsb, hotel-reservation",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=default_output,
        help=f"CSV output path (default: {default_output})",
    )
    parser.add_argument(
        "--results-dir",
        type=Path,
        default=default_results_dir,
        help=f"Directory to scan for existing result JSON files (default: {default_results_dir})",
    )
    parser.add_argument(
        "--include-existing",
        action="store_true",
        help="Emit rows even when the expected result JSON already exists",
    )

    args = parser.parse_args()
    scenarios = expand_scenarios(args.scenarios)
    existing_results = result_files(args.results_dir)
    rows: list[tuple[object, ...]] = []

    # Keep the row shape compatible with the Styx scripts:
    # workload,input_rate,n_keys,n_part,zipf,threads,time,warmup,epoch,compression,composite_keys,fallback_cache
    styx_metadata = (True, True, True)

    ycsbt_uniform_rates = [
        (100, 1),
        (200, 1),
        (300, 1),
        (500, 1),
        (700, 1),
        (1000, 1),
        (1500, 1),
        (2000, 1),
        (3000, 1),
        (3000, 2),
        (4000, 2),
        (5000, 2),
        (4000, 3),
        (5000, 3),
        (4000, 4),
        (5000, 4),
        (4400, 5),
        (4800, 5),
        (5200, 5),
        (5600, 5),
        (5000, 6),
        (5500, 6),
        (3400, 10),
        (3500, 10),
        (3600, 10),
        (3700, 10),
        (3800, 10),
        (3900, 10),
        (4000, 10),
        (4100, 10),
        (4200, 10),
        (4300, 10),
        (4400, 10),
        (4500, 10),
        (4600, 10),
        (4700, 10),
        (4800, 10),
        (4900, 10),
        (5000, 10),
        (5100, 10),
        (5200, 10),
        (5300, 10),
        (5400, 10),
        (5500, 10),
        (5600, 10),
        (5700, 10),
        (5800, 10),
        (5900, 10),
        (6000, 10),
        (10000, 10),
        (10000, 11),
        (10000, 12),
        (10000, 13),
        (10000, 14),
        (10000, 15),
        (6400, 25),
        (6800, 25),
        (7200, 25),
        (8000, 25),
        (8400, 25),
        (8600, 25),
        (9200, 25),
        (9600, 25),
    ]

    if "ycsbt_uni" in scenarios:
        for input_rate, client_threads in ycsbt_uniform_rates:
            result_name = f"ycsbt_uni_{input_rate * client_threads}.json"
            if should_emit(result_name, existing_results, args.include_existing):
                rows.append((
                    "ycsbt",
                    input_rate,
                    args.n_keys,
                    args.partitions,
                    0.0,
                    client_threads,
                    args.experiment_time,
                    args.warmup_time,
                    1000,
                    *styx_metadata,
                ))

    ycsbt_zipf_rates = [
        (200, 1),
        (700, 1),
        (1000, 1),
        (2000, 1),
        (3000, 1),
        (3000, 2),
        (3500, 2),
        (4000, 2),
    ]
    zipf_consts = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.99, 0.999]

    if "ycsbt_zipf" in scenarios:
        for input_rate, client_threads in ycsbt_zipf_rates:
            for zipf_const in zipf_consts:
                result_name = f"ycsbt_zipf_{zipf_const}_{input_rate * client_threads}.json"
                if should_emit(result_name, existing_results, args.include_existing):
                    rows.append((
                        "ycsbt",
                        input_rate,
                        args.n_keys,
                        args.partitions,
                        zipf_const,
                        client_threads,
                        args.experiment_time,
                        args.warmup_time,
                        100,
                        *styx_metadata,
                    ))

    hotel_rates = [
        (100, 1),
        (300, 1),
        (500, 1),
        (700, 1),
        (1000, 1),
        (1500, 1),
        (2000, 1),
        (3000, 1),
        (3000, 2),
        (4000, 2),
        (5000, 2),
        (4000, 3),
        (5000, 3),
        (6000, 3),
        (7000, 3),
        (5000, 5),
        (6000, 5),
        (7000, 5),
        (8000, 5),
        (9000, 5),
        (10000, 5),
        (5500, 10),
        (6000, 10),
    ]

    if "dhr" in scenarios:
        for input_rate, client_threads in hotel_rates:
            result_name = f"d_hotel_reservation_{input_rate * client_threads}.json"
            if should_emit(result_name, existing_results, args.include_existing):
                rows.append((
                    "dhr",
                    input_rate,
                    -1,
                    args.partitions,
                    0.0,
                    client_threads,
                    args.experiment_time,
                    args.warmup_time,
                    1000,
                    *styx_metadata,
                ))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerows(rows)

    print(f"Wrote {len(rows)} rows to {args.output}")


if __name__ == "__main__":
    main()
