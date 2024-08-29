import os
import argparse
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import (LogLocator, MultipleLocator, AutoMinorLocator)

from common import *


def load_result(name):
    f = open(name, "r")
    results = []
    for i, line in enumerate(f.readlines()):
        # if i % 2 == 0:
        #     continue
        if line.strip() == "":
            break
        results.append(line.split(", "))

    results = np.array(results)
    throughputs = results[:, 2]
    latency = results[:, -3]
    return throughputs, latency


def plot(data=None, output=None):
    bar_colors = [
        COLORS(0),
        COLORS(2),
        COLORS(4),
        COLORS(1),
        COLORS(3),
    ]
    markers = [
        ",:",
        ".-",
        "+-",
        "x-",
        "o-",
    ]
    labels = [
        "Shenango",
        "Skyloft (no preemption)",
        "Skyloft (20$\mu$s)",
        "Skyloft (5$\mu$s)",
        "Skyloft (5$\mu$s utimer)",
    ]

    results = {}
    res_dir = os.path.join(RES_DIR, data)
    for f in os.listdir(res_dir):
        thourghputs, latencies = load_result(os.path.join(res_dir, f))
        results[f] = {}
        print(f, thourghputs, latencies)
        for throughput, latency in zip(thourghputs, latencies):
            results[f][float(throughput) / 1000] = float(latency)

    fig = plt.figure(figsize=(6, 2.5))

    ax1 = fig.add_subplot(1, 1, 1)
    ax1.grid(which="major", axis="y", linestyle=":", alpha=0.5, zorder=0)

    ax1.set_xlim(0, 45)
    ax1.set_xticks([0, 10, 20, 30, 40])
    ax1.xaxis.set_minor_locator(MultipleLocator(2.5))

    ax1.set_ylim(0, 200)
    ax1.set_yticks([0, 50, 100, 150, 200])
    ax1.yaxis.set_minor_locator(MultipleLocator(25))

    X=np.linspace(0,50,10)
    Y=np.ones(X.size)
    line, = ax1.plot(X, 50 * Y, color = 'black', linewidth=1, linestyle="-")
    line.set_dashes((5, 5))

    for i, e in enumerate(["shenango", "skyloft_nopre", "skyloft_20us", "skyloft_5us", "skyloft_5us_utimer"]):
        # if e == "skyloft_nopre":
        #     continue
        ax1.plot(
            list(results[e].keys()),
            list(results[e].values()),
            markers[i],
            label=labels[i],
            zorder=3,
            linewidth=1,
            markersize=4,
            markeredgewidth=1,
            color=bar_colors[i],
        )

    # Create a unique legend
    handles, labels = plt.gca().get_legend_handles_labels()
    by_label = dict(zip(labels, handles))
    leg = plt.legend(
        by_label.values(), by_label.keys(), loc="best", ncol=1, frameon=False,
    )
    leg.get_frame().set_linewidth(0.0)

    fig.supylabel("99.9% Slowdown", x=0.01)
    fig.supxlabel("Throughput (kRPS)", y=0.005)

    plt.tight_layout()
    plt.subplots_adjust(top=0.97, bottom=0.18, left=0.11, right=0.97)
    plt.savefig(output)
    plt.show()


if __name__ == "__main__":
    plot("rocksdb/50-get-50-scan", "rocksdb_server.pdf")
