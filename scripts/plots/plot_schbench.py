import os
import argparse
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator, AutoMinorLocator

from common import *


parser = argparse.ArgumentParser()
parser.add_argument("--output", type=str, help="output file", default="schbench.pdf")
args = parser.parse_args()

FNAMES = [
    "linux_rr",
    "linux_cfs",
    "linux_cfs_opt",
    "linux_eevdf",
    "skyloft_eevdf",
    "skyloft_rr50us",
    "skyloft_cfs50us",
]
LABELS = [
    "Linux-RR",
    "Linux-CFS",
    "Linux-CFS-opt",
    "Linux-EEVDF",
    "Skyloft-EEVDF",
    "Skyloft-RR",
    "Skyloft-CFS",
]
MARKERS = [
    ".:",
    "+--",
    "*--",
    "*-",
    "x--",
    "x-",
    "o-",
]
COLORS = [
    COLORS(0),
    COLORS(4),
    COLORS(2),
    COLORS(6),
    COLORS(5),
    COLORS(1),
    COLORS(3),
]


def load_result(name):
    f = open(name, "r")
    results = [list(map(int, line.strip().split(","))) for line in f.readlines()[1:]]
    results = list(filter(lambda x: x[0] >= 8 and x[0] <= 96, results))
    results = np.array(results)

    cores = results[:, 0]
    lat = results[:, 1]
    lat2 = results[:, 3]
    rps = results[:, 2] / 1000
    return cores, lat, lat2, rps


def plot_one(ax, idx, x, y):
    ax.plot(
        x,
        y,
        MARKERS[idx],
        label=LABELS[idx],
        zorder=3,
        linewidth=1,
        markersize=4,
        markeredgewidth=1,
        color=COLORS[idx],
    )


def plot(data=None, output=None):
    res_dir = os.path.join(RES_DIR, data)
    fig, ax = plt.subplots(figsize=(6, 2.5))
    axs = [ax]

    axs[0].set_yscale("log", base=10)
    axs[0].set_ylim(1, 20000)
    axs[0].set_yticks([1, 10, 100, 1000, 10000])
    axs[0].set_yticklabels(["1$\mu$s", "10$\mu$s", "100$\mu$s", "1ms", "10ms"])

    for idx, ax in enumerate(axs):
        ax.grid(which="major", axis="y", linestyle="dashed", alpha=0.5, zorder=0)

        ax.set_xlim(0, 100)
        xticks = [16 * i for i in range(0, 7)]
        ax.set_xticks(xticks)
        ax.set_xticklabels(xticks)

        ax.xaxis.set_minor_locator(MultipleLocator(8))

        for i, fname in enumerate(FNAMES):
            results = load_result(f"{res_dir}/{fname}/all.csv")
            print(fname, results)
            plot_one(ax, i, results[0], results[idx + 1])

    handles, labels = ax.get_legend_handles_labels()
    plt.legend(
        handles,
        labels,
        loc="upper left",
        ncol=1,
        fontsize=8,
        frameon=False,
        handlelength=2.5,
    )

    fig.supylabel("99% wakeup latency", x=0.01)
    fig.supxlabel("Number of worker threads", y=0.005)

    plt.tight_layout()
    plt.subplots_adjust(top=0.97, bottom=0.18, left=0.13, right=0.97)
    plt.savefig(output)
    plt.show()


if __name__ == "__main__":
    plot("schbench", "schbench.pdf")
