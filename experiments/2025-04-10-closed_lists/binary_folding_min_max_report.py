import random
import sys
import heapq

def draw_gauss_domains(N, mu=2, sigma=3, min_val=1, max_val=10, seed=None):
    rng = random.Random(seed)
    domains = []
    for _ in range(N):
        value = int(round(rng.gauss(mu, sigma)))
        value = max(min_val, min(max_val, value))
        domains.append(value)
    return domains

def binary_fold_total(domains):
    value = sum(domains)
    domains = list(domains)
    while len(domains) > 1:
        new_domains = []
        i = 0
        while i + 1 < len(domains):
            node_value = domains[i] * domains[i+1]
            value += node_value
            new_domains.append(node_value)
            i += 2
        if i < len(domains):
            new_domains.append(domains[i])
        domains = new_domains
    return value

def alternating_small_large(domain_sizes):
    sorted_domains = sorted(domain_sizes)
    result = []
    left, right = 0, len(sorted_domains) - 1
    while left <= right:
        if left != right:
            result.append(sorted_domains[right])
            result.append(sorted_domains[left])
        else:
            result.append(sorted_domains[left])
        right -= 1
        left += 1
    return result

def all_largest_first(domain_sizes):
    return sorted(domain_sizes, reverse=True)

def huffman_optimal(domains):
    heap = list(domains)
    heapq.heapify(heap)
    value = sum(domains)
    while len(heap) > 1:
        a = heapq.heappop(heap)
        b = heapq.heappop(heap)
        prod = a * b
        value += prod
        heapq.heappush(heap, prod)
    return value

print("N, min_mean, max_mean, mean_mean, opt_mean, below_max_pct, below_mean_pct, below_opt_pct, below_opt_mean_pct")

max_N = 40
M = 10
mu = 2
sigma = 3
if len(sys.argv) > 1:
    try:
        max_N = int(sys.argv[1])
        if len(sys.argv) > 2:
            M = int(sys.argv[2])
        if len(sys.argv) > 3:
            sigma = float(sys.argv[3])
    except Exception as e:
        print("Usage: python binary_folding_min_max_report.py [max_N] [M] [sigma]")
        sys.exit(1)

for N in range(8, max_N+1):
    mean_min = 0
    mean_max = 0
    mean_mean = 0
    mean_opt = 0
    min_pcts = []
    mean_pcts = []
    opt_pcts = []
    opt_mean_pcts = []
    for m in range(M):
        domain_sizes = draw_gauss_domains(N=N, mu=mu, sigma=sigma, min_val=1, max_val=10, seed=42 + m)
        min_perm = alternating_small_large(domain_sizes)
        max_perm = all_largest_first(domain_sizes)
        mean_perm = domain_sizes
        min_value = binary_fold_total(min_perm)
        max_value = binary_fold_total(max_perm)
        mean_value = binary_fold_total(mean_perm)
        opt_value = huffman_optimal(domain_sizes)
        mean_min += min_value
        mean_max += max_value
        mean_mean += mean_value
        mean_opt += opt_value
        min_pcts.append(100.0 * (max_value - min_value) / max_value)
        mean_pcts.append(100.0 * (mean_value - min_value) / mean_value)
        opt_pcts.append(100.0 * (max_value - opt_value) / max_value)
        opt_mean_pcts.append(100.0 * (mean_value - opt_value)/mean_value)
    mean_min /= M
    mean_max /= M
    mean_mean /= M
    mean_opt /= M
    mean_min_pct = sum(min_pcts) / M
    mean_mean_pct = sum(mean_pcts) / M
    mean_opt_pct = sum(opt_pcts) / M
    mean_opt_mean_pct = sum(opt_mean_pcts) / M
    print(f"{N}, {mean_min:.3e}, {mean_max:.3e}, {mean_mean:.3e}, {mean_opt:.3e}, "
          f"{mean_min_pct:.2f}%, {mean_mean_pct:.2f}%, {mean_opt_pct:.2f}%, {mean_opt_mean_pct:.2f}%")
