import json
import matplotlib.pyplot as plt
import os
import numpy as np
from matplotlib.ticker import ScalarFormatter, LogLocator

properties_dir = "experiments/atd/data/2025-05-13-labelreduction-eval/"

# Load the data from the JSON file
with open(properties_dir + "properties", "r") as f:
    raw_data = json.load(f)

# Group the data by problem, separating "with" and "without" label reduction
grouped = {}
for full_key, stats in raw_data.items():
    # Skip entries without transition data
    num_transitions = stats.get("num_transitions")
    if num_transitions is None:
        continue
        
    # Parse the keys to identify with/without label reduction
    if full_key.startswith("with label reduction-"):
        base_key = full_key[len("with label reduction-"):]
        grouped.setdefault(base_key, {})["with_label"] = num_transitions
    elif full_key.startswith("without label reduction-"):
        base_key = full_key[len("without label reduction-"):]
        grouped.setdefault(base_key, {})["without_label"] = num_transitions

# Extract data for analysis
ratios = []
with_vals = []
without_vals = []
labels = []
for key, values in grouped.items():
    with_val = values.get("with_label")
    without_val = values.get("without_label")
    if with_val is not None and without_val not in (None, 0):
        ratio = with_val / without_val
        ratios.append(ratio)
        with_vals.append(with_val)
        without_vals.append(without_val)
        labels.append(key)

# Write results to a formatted table
output_file = "ratio.txt"
with open(properties_dir + output_file, "w") as file:
    file.write(f"{'Key':<50} {'With Label':>15} {'Without Label':>15} {'Ratio':>10}\n")
    file.write("-" * 90 + "\n")
    for key, w_val, wo_val, ratio in zip(labels, with_vals, without_vals, ratios):
        file.write(f"{key:<50} {w_val:>15d} {wo_val:>15d} {ratio:>10.3f}\n")

print(f"Table written to {os.path.abspath(output_file)}")
#---------------------------------------------------------------------------------------------------

# Create scatter plot with colored points based on ratio
plt.figure(figsize=(10, 6))

# Create scatter plot with color based on ratio values
scatter = plt.scatter(without_vals, with_vals, c=ratios, cmap='viridis', 
                      edgecolor='k', alpha=0.75)

# Add colorbar to show ratio values
cbar = plt.colorbar(scatter, label='With/Without Ratio')

# Add x=y line (where label reduction has no effect)
max_val = max(max(without_vals), max(with_vals))
min_val = min(min(without_vals), min(with_vals))
plt.plot([min_val, max_val], [min_val, max_val], 'r--', linewidth=2, label='x=y line')

# Use logarithmic scales for both axes
plt.xscale('log')
plt.yscale('log')

# Add labels and title
plt.xlabel('Transitions WITHOUT Label Reduction', fontsize=12)
plt.ylabel('Transitions WITH Label Reduction', fontsize=12)
plt.title('Scatterplot of Transitions With vs Without Label Reduction', fontsize=14)
plt.grid(True, which="both", ls="--", linewidth=0.5)
plt.legend()

# Calculate and display statistics
below_xy = sum(1 for w, wo in zip(with_vals, without_vals) if w < wo)
total = len(with_vals)
percent_improved = (below_xy / total) * 100

# Add text annotation with statistics
plt.annotate(f'Label reduction reduced transitions in {below_xy}/{total} cases ({percent_improved:.1f}%)',
             xy=(0.05, 0.05), xycoords='axes fraction', fontsize=10,
             bbox=dict(boxstyle="round,pad=0.5", fc="white", alpha=0.8))

# Save the plot
plt.tight_layout()
plt.savefig(properties_dir + 'label_reduction_comparison.png', dpi=300)
print(f"Plot saved to {os.path.abspath('label_reduction_comparison.png')}")

# Show the plot (optional, can be commented out for headless environments)
plt.show()
#---------------------------------------------------------------------------------------------------

# Create a histogram of the ratios
plt.figure(figsize=(12, 8))
plt.hist(ratios, bins=30, alpha=0.7, color='green', edgecolor='black')

# Add vertical line at x=1 (no change)
plt.axvline(x=1, color='r', linestyle='--', linewidth=2, label='No change (ratio=1)')

# Add labels and title
plt.xlabel('Ratio (With/Without Label Reduction)', fontsize=14)
plt.ylabel('Frequency', fontsize=14)
plt.title('Distribution of Transition Count Ratios', fontsize=16)
plt.legend(fontsize=12)

# Add grid
plt.grid(True, alpha=0.3, linestyle='--')

# Calculate mean and median
mean_ratio = np.mean(ratios)
median_ratio = np.median(ratios)

# Add text annotation with statistics
plt.annotate(f'Mean ratio: {mean_ratio:.3f}\nMedian ratio: {median_ratio:.3f}',
             xy=(0.05, 0.95), xycoords='axes fraction', fontsize=12,
             bbox=dict(boxstyle="round,pad=0.5", fc="white", alpha=0.8),
             verticalalignment='top')

# Save the histogram
plt.tight_layout()
plt.savefig(properties_dir + 'label_reduction_ratio_histogram.png', dpi=300)
print(f"Histogram saved to {os.path.abspath('label_reduction_ratio_histogram.png')}")

# Show the histogram (optional)
plt.show()
