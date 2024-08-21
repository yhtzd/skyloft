import os
import matplotlib.pyplot as plt

plt.rcParams['axes.labelsize'] = 11

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
RES_DIR = os.path.join(BASE_DIR, "../../paper_results")

COLORS = plt.get_cmap("tab10")
COLORS2 = plt.get_cmap("tab20")
COLORS3 = plt.get_cmap("tab20c")
