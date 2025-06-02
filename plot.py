import json
import matplotlib.pyplot as plt
import os
import numpy as np
import scipy

from matplotlib.ticker import ScalarFormatter

properties_dir = "experiments/atd/data/2025-05-13-labelreduction-cluster-eval/"

# Load the data
with open(properties_dir + "properties", "r") as f:
    raw_data = json.load(f)

# Group by base problem name
grouped = {}

for full_key, stats in raw_data.items():
    num_transitions = stats.get("num_transitions")
    if num_transitions is None:
        continue

    if full_key.startswith("without label reduction-"):
        base_key = full_key[len("without label reduction-"):]
        grouped.setdefault(base_key, {})["without"] = num_transitions

    elif full_key.startswith("with label reduction (min_ops_per_label="):
        end_idx = full_key.index(")-")
        param_val = int(full_key[len("with label reduction (min_ops_per_label="):end_idx])
        base_key = full_key[end_idx + 2:]
        grouped.setdefault(base_key, {}).setdefault("with", {})[param_val] = num_transitions

# Now analyze and plot per min_ops_per_label
all_ratios = {}  # key: min_ops value -> list of ratios
for min_ops in [1, 2, 3, 4, 5, 10, 20, 50, 100]:
    # Plot top 10 domains by max num_labels for this min_ops
    max_num_labels = []
    num_labels_keys = []

    for key, data in grouped.items():
        w_data_key = f"with label reduction (min_ops_per_label={min_ops})-{key}"
        w_data = raw_data.get(w_data_key)
        if w_data is None:
            continue
        nl = w_data.get("num_labels")
        if nl is not None:
            max_num_labels.append(nl)
            num_labels_keys.append(key)

    ratios = []
    with_vals = []
    without_vals = []
    labels = []

    for key, data in grouped.items():
        wo_val = data.get("without")
        w_val = data.get("with", {}).get(min_ops)

        if wo_val not in (None, 0) and w_val is not None:
            ratio = w_val / wo_val
            ratios.append(ratio)
            with_vals.append(w_val)
            without_vals.append(wo_val)
            labels.append(key)

    if not ratios:
        continue

    all_ratios[min_ops] = ratios

    # Write ratio table
    table_file = f"num_transitions_min_ops_{min_ops}.txt"
    with open(properties_dir + table_file, "w") as f:
        f.write(f"{'Key':<50} {'Without':>15} {'With':>15} {'Transition Ratio':>10}\n")
        f.write("-" * 90 + "\n")
        for key, wo_val, w_val, ratio in zip(labels, without_vals, with_vals, ratios):
            f.write(f"{key:<50} {wo_val:>15d} {w_val:>15d} {ratio:>10.3f}\n")
    print(f"Wrote {table_file}")

    # Scatter plot num_transitions with vs without label reduction (colored by ratio)
    plt.figure(figsize=(10, 6))
    ratios_array = np.array(with_vals) / np.array(without_vals)
    scatter = plt.scatter(without_vals, with_vals, c=ratios_array, cmap='viridis', edgecolor='k', alpha=0.75)
    cbar = plt.colorbar(scatter, label='With/Without Transition Ratio')

    max_val = max(max(without_vals), max(with_vals))
    min_val = min(min(without_vals), min(with_vals))

    # Diagonal x=y line
    plt.plot([min_val, max_val], [min_val, max_val], 'r--', label='x = y')

    plt.xscale('log')
    plt.yscale('log')
    plt.xlabel('Transitions Without Label Reduction', fontsize=12)
    plt.ylabel(f'Transitions With Label Reduction (min_ops={min_ops})', fontsize=12)
    plt.title(f'Scatterplot: Transitions With vs Without (min_ops={min_ops})', fontsize=14)
    plt.grid(True, which="both", linestyle='--', linewidth=0.5)
    plt.legend()

    improved = sum(1 for w, wo in zip(with_vals, without_vals) if w < wo)
    total = len(with_vals)
    percent = (improved / total) * 100
    plt.annotate(f'{improved}/{total} cases improved ({percent:.1f}%)',
                 xy=(0.05, 0.05), xycoords='axes fraction', fontsize=10,
                 bbox=dict(boxstyle="round,pad=0.5", fc="white", alpha=0.8))

    scatter_file = f"scatter_num_transitions_min_ops_{min_ops}.png"
    plt.tight_layout()
    plt.savefig(properties_dir + scatter_file, dpi=300)
    plt.close()
    print(f"Saved scatter plot: {scatter_file}")

    # Histogram of ratios
    plt.figure(figsize=(12, 8))
    plt.hist(ratios, bins=30, alpha=0.7, color='green', edgecolor='black')
    plt.axvline(x=1, color='r', linestyle='--', linewidth=2, label='Ratio = 1')
    plt.xlabel('num_transitions Ratio (With / Without)', fontsize=14)
    plt.ylabel('Frequency', fontsize=14)
    plt.title(f'num_transitions Ratio Distribution (min_ops={min_ops})', fontsize=16)
    plt.grid(True, linestyle='--', alpha=0.4)
    mean_ratio = np.mean(ratios)
    median_ratio = np.median(ratios)
    plt.annotate(f'Mean: {mean_ratio:.3f}\nMedian: {median_ratio:.3f}',
                 xy=(0.05, 0.95), xycoords='axes fraction', fontsize=12,
                 verticalalignment='top',
                 bbox=dict(boxstyle="round,pad=0.5", fc="white", alpha=0.8))
    plt.legend()
    hist_file = f"hist_num_transitions_min_ops_{min_ops}.png"
    plt.tight_layout()
    plt.savefig(properties_dir + hist_file, dpi=300)
    plt.close()
    print(f"Saved histogram: {hist_file}")

    # cp_time ratio scatter plot with annotations, excluding cp_time < 1s
    cp_time_ratios = []
    ratio_for_cp = []
    cp_labels = []

    for key, data in grouped.items():
        wo_data = raw_data.get(f"without label reduction-{key}")
        w_data_key = f"with label reduction (min_ops_per_label={min_ops})-{key}"
        w_data = raw_data.get(w_data_key)

        if not (wo_data and w_data):
            continue

        cp_wo = wo_data.get("cp_time")
        cp_w = w_data.get("cp_time")
        wo_val = data.get("without")
        w_val = data.get("with", {}).get(min_ops)

        if None in (cp_wo, cp_w, wo_val, w_val) or wo_val == 0:
            continue

        # Exclude trivial cases with fast cp_time
        if cp_wo < 1 or cp_w < 1:
            continue

        trans_ratio = w_val / wo_val
        cp_ratio = cp_w / cp_wo

        ratio_for_cp.append(trans_ratio)
        cp_time_ratios.append(cp_ratio)
        cp_labels.append(key)

    if cp_time_ratios:
        plt.figure(figsize=(10, 6))
        scatter = plt.scatter(ratio_for_cp, cp_time_ratios, c=cp_time_ratios,
                            cmap='coolwarm', edgecolor='k', alpha=0.75)

        # Reference lines
        plt.axvline(x=1, color='gray', linestyle='--', linewidth=1)
        plt.axhline(y=1, color='gray', linestyle='--', linewidth=1)

        # Count cases per quadrant (only left half, as right half doesn't exist)
        total = len(cp_time_ratios)
        q1 = sum(1 for x, y in zip(ratio_for_cp, cp_time_ratios) if x < 1 and y < 1)
        q2 = sum(1 for x, y in zip(ratio_for_cp, cp_time_ratios) if x < 1 and y >= 1)

        q1_pct = (q1 / total) * 100
        q2_pct = (q2 / total) * 100

        # Annotations on upper left
        plt.text(0.3, 3, f'{q2}/{total} cases\n({q2_pct:.1f}%)',
                fontsize=11, ha='center', va='center',
                bbox=dict(boxstyle="round,pad=0.4", fc="khaki", alpha=0.3))
        plt.text(0.3, 0.3, f'{q1}/{total} cases\n({q1_pct:.1f}%)',
                fontsize=11, ha='center', va='center',
                bbox=dict(boxstyle="round,pad=0.4", fc="lightgreen", alpha=0.3))

        plt.xscale('log')
        plt.yscale('log')
        plt.xlabel('Transition Ratio (With / Without)', fontsize=12)
        plt.ylabel('cp_time Ratio (With / Without)', fontsize=12)
        plt.title(f'cp_time vs Transition Ratio (min_ops={min_ops})\n(Filtered: cp_time ≥ 1s)', fontsize=14)
        plt.grid(True, which="both", linestyle='--', linewidth=0.5)

        cbar = plt.colorbar(scatter, label='cp_time Ratio')
        plt.tight_layout()

        cp_plot_file = f"scatter_cp_time_min_ops_{min_ops}.png"
        plt.savefig(properties_dir + cp_plot_file, dpi=300)
        plt.close()
        print(f"Saved simplified annotated cp_time vs transition ratio scatter: {cp_plot_file}")

    # Histogram of cp_time ratios
    if cp_time_ratios:
        plt.figure(figsize=(10, 6))

        plt.hist(cp_time_ratios, bins=30, color='skyblue', edgecolor='black', alpha=0.8)
        plt.axvline(x=1, color='red', linestyle='--', linewidth=2, label='Ratio = 1')

        mean_ratio = np.mean(cp_time_ratios)
        median_ratio = np.median(cp_time_ratios)
        improved = sum(1 for r in cp_time_ratios if r < 1)
        total = len(cp_time_ratios)
        percent = (improved / total) * 100

        plt.xlabel('cp_time Ratio (With / Without)', fontsize=12)
        plt.ylabel('Frequency', fontsize=12)
        plt.title(f'cp_time Ratio Distribution (min_ops={min_ops})\n(Filtered: cp_time ≥ 1s)', fontsize=14)
        plt.grid(True, linestyle='--', alpha=0.5)
        plt.legend()

        summary_text = (
            f"Total cases: {total}\n"
            f"Improved: {improved} ({percent:.1f}%)\n"
            f"Mean ratio: {mean_ratio:.3f}\n"
            f"Median ratio: {median_ratio:.3f}"
        )
        plt.annotate(summary_text,
                    xy=(0.02, 0.98), xycoords='axes fraction',
                    ha='left', va='top', fontsize=10,
                    bbox=dict(boxstyle="round,pad=0.5", fc="white", alpha=0.9))

        hist_file = f"hist_cp_time_min_ops_{min_ops}.png"
        plt.tight_layout()
        plt.savefig(properties_dir + hist_file, dpi=300)
        plt.close()
        print(f"Saved cp_time histogram: {hist_file}")

    if cp_time_ratios and ratio_for_cp:
        plt.figure(figsize=(10, 6))
        scatter = plt.scatter(ratio_for_cp, cp_time_ratios, c=cp_time_ratios,
                            cmap='plasma', edgecolor='k', alpha=0.75)

        # Regression line (optional)
        from scipy.stats import linregress
        log_x = np.log10(ratio_for_cp)
        log_y = np.log10(cp_time_ratios)
        slope, intercept, r_value, _, _ = linregress(log_x, log_y)
        reg_x = np.linspace(min(log_x), max(log_x), 100)
        reg_y = slope * reg_x + intercept
        plt.plot(10**reg_x, 10**reg_y, color='black', linestyle='--', label=f'Fit (r={r_value:.2f})')

        # Add vertical and horizontal lines at ratio = 1 (10^0)
        plt.axvline(x=1, color='red', linestyle=':', linewidth=1)
        plt.axhline(y=1, color='red', linestyle=':', linewidth=1)

        plt.xscale('log')
        plt.yscale('log')
        plt.xlabel('num_transitions Ratio (With / Without)', fontsize=12)
        plt.ylabel('cp_time Ratio (With / Without)', fontsize=12)
        plt.title(f'Correlation: num_transitions vs cp_time Ratio (min_ops={min_ops})\n(Filtered: cp_time ≥ 1s)', fontsize=14)
        plt.grid(True, which="both", linestyle='--', linewidth=0.5)
        cbar = plt.colorbar(scatter, label='cp_time Ratio')
        plt.legend()
        plt.tight_layout()

        corr_plot_file = f"correlation_cp_time_vs_num_transitions_min_ops_{min_ops}.png"
        plt.savefig(properties_dir + corr_plot_file, dpi=300)
        plt.close()
        print(f"Saved correlation plot: {corr_plot_file}")

    # Scatter plot cp_time with vs without label reduction (filtered cp_time >=1)
    if cp_time_ratios:
        cp_wo_vals = []
        cp_w_vals = []
        cp_labels_filtered = []

        for key, data in grouped.items():
            wo_data = raw_data.get(f"without label reduction-{key}")
            w_data_key = f"with label reduction (min_ops_per_label={min_ops})-{key}"
            w_data = raw_data.get(w_data_key)

            if not (wo_data and w_data):
                continue

            cp_wo = wo_data.get("cp_time")
            cp_w = w_data.get("cp_time")
            if cp_wo is None or cp_w is None:
                continue

            if cp_wo < 1 or cp_w < 1:
                continue

            cp_wo_vals.append(cp_wo)
            cp_w_vals.append(cp_w)
            cp_labels_filtered.append(key)

        if cp_wo_vals:
            plt.figure(figsize=(12, 8))
            ratio_cp_vals = np.array(cp_w_vals) / np.array(cp_wo_vals)
            scatter = plt.scatter(cp_wo_vals, cp_w_vals, c=ratio_cp_vals,
                                cmap='viridis', edgecolor='k', alpha=0.75, 
                                s=60, linewidth=0.5)
            cbar = plt.colorbar(scatter, label='cp_time Ratio')
            cbar.ax.tick_params(labelsize=11)

            min_cp = min(min(cp_wo_vals), min(cp_w_vals))
            max_cp = max(max(cp_wo_vals), max(cp_w_vals))

            # Main diagonal line (no change)
            plt.plot([min_cp, max_cp], [min_cp, max_cp], 'r--', linewidth=2, 
                     label='x = y', alpha=0.8)

            # Simplified ratio lines - only show key performance boundaries
            key_ratios = [0.5, 2.0]  # 50% faster, 2x slower
            colors = ['green', 'orange']
            labels = ['2× Faster', '2× Slower']
            
            for r, color, label in zip(key_ratios, colors, labels):
                plt.plot([min_cp, max_cp], [v * r for v in [min_cp, max_cp]],
                        linestyle='--', linewidth=2, color=color, alpha=0.7,
                        label=label)

            plt.xscale('log')
            plt.yscale('log')
            plt.xlabel('cp_time Without Label Reduction (s)', fontsize=14, fontweight='bold')
            plt.ylabel(f'cp_time With Label Reduction (min_ops={min_ops}) (s)', 
                      fontsize=14, fontweight='bold')
            plt.title(f'cp_time Comparison (min_ops={min_ops})\nFiltered: cp_time ≥ 1s', 
                     fontsize=16, fontweight='bold', pad=20)
            
            # Improved grid - less intrusive
            plt.grid(True, which="major", linestyle='-', linewidth=0.8, alpha=0.3)
            plt.grid(True, which="minor", linestyle=':', linewidth=0.5, alpha=0.2)
            
            plt.legend(fontsize=11, loc='upper left')

            improved = sum(1 for w, wo in zip(cp_w_vals, cp_wo_vals) if w < wo)
            total = len(cp_w_vals)
            percent = (improved / total) * 100

            # Better positioned and styled annotation
            plt.annotate(f'Time Improved: {improved}/{total} cases ({percent:.1f}%)',
                         xy=(0.05, 0.05), xycoords='axes fraction', fontsize=10,
                         bbox=dict(boxstyle="round,pad=0.5", fc="white", alpha=0.8))

            cp_scatter_file = f"scatter_cp_time_min_ops_{min_ops}.png"
            plt.tight_layout()
            plt.savefig(properties_dir + cp_scatter_file, dpi=300, bbox_inches='tight')
            plt.close()
            print(f"Saved cp_time scatter plot: {cp_scatter_file}")
    # Identify worst cp_time ratio cases
    # Collect and combine key, cp_time ratio, and transition ratio
    combined_ratios = list(zip(cp_labels, cp_time_ratios, ratio_for_cp))

    # Sort by cp_time ratio descending (worst first)
    combined_ratios.sort(key=lambda x: x[1], reverse=True)

    top_n = 10
    worst_cases_file = f"worst_cp_time_num_transitions_min_ops_{min_ops}.txt"
    with open(properties_dir + worst_cases_file, "w") as f:
        f.write(f"Top {top_n} worst cp_time ratios (With / Without), min_ops={min_ops}\n")
        f.write("-" * 90 + "\n")
        f.write(f"{'Key':<50} {'cp_time Ratio':>15} {'num_transitions Ratio':>15}\n")
        f.write("-" * 90 + "\n")
        for key, cp_ratio, trans_ratio in combined_ratios[:top_n]:
            f.write(f"{key:<50} {cp_ratio:>15.3f} {trans_ratio:>15.3f}\n")

    print(f"Saved worst cp_time ratio list with transition ratios: {worst_cases_file}")


