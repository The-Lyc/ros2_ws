import matplotlib.pyplot as plt
import seaborn as sns
from collections import defaultdict
import re

# 1. 解析数据
latency_data = defaultdict(list)
current_key = None

with open("output.txt", "r") as f:
    for line in f:
        line = line.strip()
        if line.startswith("Key"):
            match = re.match(r"Key\s+(\S+):", line)
            if match:
                current_key = match.group(1)
        elif line.endswith("ms") and current_key:
            latency = int(line.replace("ms", "").strip())
            latency_data[current_key].append(latency)

# 2. 准备绘图数据
data = []
keys = []
for key, latencies in latency_data.items():
    data.extend(latencies)
    keys.extend([key] * len(latencies))

# 3. 绘图
plt.figure(figsize=(10, 6))
sns.boxplot(x=keys, y=data)
plt.xticks(rotation=45)
plt.xlabel("Key")
plt.ylabel("Latency (ms)")
plt.title("Latency Boxplot by Key")
plt.tight_layout()
plt.savefig("latency_boxplot.png")
plt.show()

