from collections import defaultdict

# 读取 log 文件
with open("data.txt", "r") as f:
    lines = f.readlines()

# 存储 key 对应的 latency 列表
key_latency_map = defaultdict(list)

for line in lines:
    try:
        latency_part = line.split("latency is:")[1].split("ms")[0].strip()
        key_part = line.split("key is:")[1].split(",")[0].strip()
        latency = int(latency_part)
        key_latency_map[key_part].append(latency)
    except (IndexError, ValueError):
        continue  # 跳过格式异常的行

# 写入输出文件
with open("output_by_key.txt", "w") as f:
    for key, latencies in key_latency_map.items():
        f.write(f"Key {key}:\n")
        for latency in latencies:
            f.write(f"  {latency}ms\n")
        f.write("\n")

