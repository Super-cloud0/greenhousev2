"""
plot_growth.py
Строит график динамики параметров теплицы (влажность почвы, температура,
влажность воздуха, свет) из data/telemetry.csv, который пишет
greenhouse_backend.py.

Использование:
    python scripts/plot_growth.py
    python scripts/plot_growth.py --csv data/telemetry.csv --out report_chart.png
"""

import argparse
import csv
import os
import sys
from datetime import datetime

import matplotlib
matplotlib.use("Agg")  # рендер в файл, без GUI
import matplotlib.pyplot as plt


def load_rows(csv_path):
    rows = []
    with open(csv_path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            try:
                rows.append({
                    "ts": datetime.fromisoformat(r["timestamp"]),
                    "soil": float(r["soil_pct"]),
                    "temp": float(r["temp_c"]),
                    "hum": float(r["humidity_pct"]),
                    "pump": int(r["pump_on"]),
                    "light": int(r["light_on"]),
                })
            except (ValueError, KeyError):
                continue  # битые строки пропускаем
    return rows


def plot(rows, out_path):
    if not rows:
        print("CSV пуст: телеметрия ещё не собрана.")
        sys.exit(1)

    ts     = [r["ts"] for r in rows]
    soil   = [r["soil"] for r in rows]
    temp   = [r["temp"] for r in rows]
    hum    = [r["hum"] for r in rows]
    light  = [r["light"] for r in rows]

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 7), sharex=True)

    ax1.plot(ts, soil, color="#2e7d32", label="Влажность почвы, %")
    ax1.axhline(30, color="#c62828", linestyle="--", linewidth=1, label="Порог полива (30%)")
    ax1.set_ylabel("Влажность почвы, %")
    ax1.set_ylim(0, 100)
    ax1.legend(loc="upper right")
    ax1.set_title("Динамика параметров теплицы")

    ax2.plot(ts, temp, color="#e65100", label="Температура, °C")
    ax2.plot(ts, hum,  color="#0277bd", label="Влажность воздуха, %")
    ax2.set_ylabel("°C / %")
    ax2.set_xlabel("Время")
    ax2.legend(loc="upper right")

    # Периоды включённой досветки
    prev = None
    start = None
    for t, l in zip(ts, light):
        if l and prev != 1:
            start = t
        if not l and prev == 1 and start is not None:
            ax2.axvspan(start, t, color="gold", alpha=0.15)
        prev = l
    if prev == 1 and start is not None:
        ax2.axvspan(start, ts[-1], color="gold", alpha=0.15)

    fig.autofmt_xdate()
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"Готово: {out_path} ({len(rows)} точек, {ts[0]} — {ts[-1]})")


def main():
    parser = argparse.ArgumentParser(description="Построить график роста/телеметрии теплицы")
    default_csv = os.path.join(os.path.dirname(__file__), "..", "data", "telemetry.csv")
    parser.add_argument("--csv", default=default_csv, help="путь к telemetry.csv")
    parser.add_argument("--out", default="growth_chart.png", help="куда сохранить график")
    args = parser.parse_args()

    if not os.path.exists(args.csv):
        print(f"Файл не найден: {args.csv}\n"
              f"CSV создаётся автоматически при первом сообщении телеметрии.")
        sys.exit(1)

    plot(load_rows(args.csv), args.out)


if __name__ == "__main__":
    main()
