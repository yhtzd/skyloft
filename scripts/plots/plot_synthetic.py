import os
import argparse
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator, AutoMinorLocator
from matplotlib.lines import Line2D

from common import *

BAR_COLORS = {
    "cfs": COLORS(0),
    "skyloft-30us": COLORS(1),
    "ghost-30us": COLORS(4),
    "shinjuku-30us": COLORS(2),
}
MARKERS = {
    "cfs": ".:",
    "ghost-30us": "+-",
    "shinjuku-30us": "*-",
    "skyloft-30us": "x-",
}
LABELS = {
    "cfs": "CFS",
    "ghost-30us": "ghOSt (30$\mu$s)",
    "skyloft-30us": "Skyloft (30$\mu$s)",
    "shinjuku-30us": "Shinjuku (30$\mu$s)",
}
FNAMES = [
    "cfs",
    "ghost-30us",
    "shinjuku-30us",
    "skyloft-30us",
]

def load_result(name):
    f = open(name, "r")
    results = np.array([line.split(",") for line in f.readlines()])
    throughputs = results[:, 1]
    lat_99ths = results[:, -4]
    return throughputs, lat_99ths


def load_result_be(name):
    f = open(name, "r")
    results = np.array([line.split(",") for line in f.readlines()])
    throughputs = results[:, 0]
    cpu_shares = results[:, -1]
    return throughputs, cpu_shares


def plot(id, results, output=None):
    fig, ax1 = plt.subplots(figsize=(4, 2.5))

    ax1.set_ylabel("99% Latency ($\mu$s)")
    ax1.get_yaxis().set_label_coords(-0.12, 0.5)
    ax1.set_xlabel("Throughput (kRPS)")
    ax1.grid(which="major", axis="y", linestyle=":", alpha=0.5, zorder=0)

    if id < 2:
        ax_xticks = range(0, 351, 100)
        ax1.set_xlim(0, 360)
        ax1.set_xticks(ax_xticks)
        ax1.set_xticklabels([xtick for xtick in ax_xticks])
        ax1.xaxis.set_minor_locator(MultipleLocator(20))

        ax1_yticks = range(0, 1001, 200)
        ax1.set_ylim(-30, 1000)
        ax1.set_yticks(ax1_yticks, minor=False)
        ax1.set_yticklabels([ytick for ytick in ax1_yticks])
        ax1.yaxis.set_minor_locator(MultipleLocator(100))
    else:
        ax1.set_ylabel("Batch CPU Share")
        ax_xticks = range(0, 351, 100)
        ax1.set_xlim(0, 310)
        ax1.set_xticks(ax_xticks)
        ax1.set_xticklabels([xtick for xtick in ax_xticks])
        ax1.xaxis.set_minor_locator(MultipleLocator(20))

        ax1_yticks = [0, 0.2, 0.4, 0.6, 0.8, 1]
        ax1.set_ylim(-0.05, 1)
        ax1.set_yticks(ax1_yticks, minor=False)
        ax1.set_yticklabels([ytick for ytick in ax1_yticks])

        ax1.text( 100, 0.2, "➊", fontsize=14, va='center', ha='center')
        ax1.quiver(110, 0.16, 20, -0.12, scale=1, scale_units='xy', angles='xy',
            width=0.004, headwidth=5
        )

    for e in FNAMES:
        ax1.plot(
            list(results[e].keys()),
            list(results[e].values()),
            MARKERS[e],
            label=LABELS[e],
            zorder=3,
            linewidth=1,
            markersize=4,
            color=BAR_COLORS[e],
        )

    if id == 0:
        # Create a unique legend
        handles, labels = plt.gca().get_legend_handles_labels()
        by_label = dict(zip(labels, handles))
        leg = plt.legend(by_label.values(), by_label.keys(),
            loc="best", ncol=1, frameon=False, handlelength=2.5
        )
        leg.get_frame().set_linewidth(0.0)

    plt.tight_layout()
    plt.subplots_adjust(top=0.97, bottom=0.18, left=0.15, right=0.97)
    plt.savefig(output)
    plt.show()


if __name__ == "__main__":
    results = {}
    res_dir = os.path.join(RES_DIR, "synthetic", "99.5-4-0.5-10000")
    for f in os.listdir(res_dir):
        results[f] = {}
        thourghputs, lat_99ths = load_result(os.path.join(res_dir, f))
        for throughput, lat_99th in zip(thourghputs, lat_99ths):
            results[f][float(throughput) / 1000] = int(lat_99th) / 1000
    plot(0, results, "synthetic-a.pdf")

    results_lc = {}
    results_be = {}
    res_dir = os.path.join(RES_DIR, "synthetic", "99.5-4-0.5-10000-lcbe")
    for f in os.listdir(res_dir):
        if "-lc" in f:
            thourghputs, lat_99ths = load_result(os.path.join(res_dir, f))
            res = results_lc
            values = map(lambda x: int(x) / 1000, lat_99ths)
        elif "-be" in f:
            thourghputs, cpu_shares = load_result_be(os.path.join(res_dir, f))
            res = results_be
            values = map(float, cpu_shares)

        f = f[:-3]
        res[f] = {}
        print(f)
        for throughput, v in zip(thourghputs, values):
            print(throughput, v)
            res[f][float(throughput) / 1000] = v
    plot(1, results_lc, "synthetic-b.pdf")
    plot(2, results_be, "synthetic-c.pdf")
