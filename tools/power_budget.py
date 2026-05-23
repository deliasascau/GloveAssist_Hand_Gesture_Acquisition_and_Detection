#!/usr/bin/env python3
"""Estimate GloveAssist average current and battery runtime.

Example:
  python tools/power_budget.py --battery-mah 1200 \
    --state idle,85,0.80 --state gesture,130,0.15 --state alarm,280,0.05
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass


@dataclass
class State:
    name: str
    current_ma: float
    duty: float


def parse_state(text: str) -> State:
    parts = [p.strip() for p in text.split(",")]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("state must be NAME,CURRENT_MA,DUTY")

    name = parts[0]
    if not name:
        raise argparse.ArgumentTypeError("state name cannot be empty")

    try:
        current_ma = float(parts[1])
        duty = float(parts[2])
    except ValueError as exc:
        raise argparse.ArgumentTypeError("current and duty must be numbers") from exc

    if current_ma < 0.0:
        raise argparse.ArgumentTypeError("current must be >= 0")
    if duty < 0.0 or duty > 1.0:
        raise argparse.ArgumentTypeError("duty must be between 0 and 1")

    return State(name=name, current_ma=current_ma, duty=duty)


def default_states() -> list[State]:
    return [
        State("idle_ble", 85.0, 0.80),
        State("gesture_ble", 130.0, 0.15),
        State("wifi_mqtt_publish", 210.0, 0.04),
        State("local_alarm", 280.0, 0.01),
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--battery-mah", type=float, default=1200.0)
    parser.add_argument("--usable-percent", type=float, default=80.0)
    parser.add_argument(
        "--state",
        action="append",
        type=parse_state,
        help="Power state as NAME,CURRENT_MA,DUTY. Duty is 0..1.",
    )
    args = parser.parse_args()

    states = args.state if args.state else default_states()
    duty_sum = sum(state.duty for state in states)
    if duty_sum <= 0.0:
        parser.error("sum of duty cycles must be > 0")
    if duty_sum > 1.0:
        parser.error(f"sum of duty cycles is {duty_sum:.3f}, must be <= 1")

    avg_ma = sum(state.current_ma * state.duty for state in states)
    usable_mah = args.battery_mah * (args.usable_percent / 100.0)
    runtime_h = usable_mah / avg_ma if avg_ma > 0.0 else 0.0

    print("Power Budget")
    print("------------")
    for state in states:
        contribution = state.current_ma * state.duty
        print(
            f"{state.name:18s} current={state.current_ma:7.2f} mA "
            f"duty={state.duty:5.2%} contribution={contribution:7.2f} mA"
        )

    if duty_sum < 1.0:
        print(f"{'unmodelled':18s} duty={(1.0 - duty_sum):5.2%} contribution=   0.00 mA")

    print()
    print(f"Average current : {avg_ma:.2f} mA")
    print(f"Battery usable  : {usable_mah:.1f} mAh ({args.usable_percent:.0f}% of {args.battery_mah:.0f} mAh)")
    print(f"Estimated time  : {runtime_h:.2f} h ({runtime_h / 24.0:.2f} days)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
