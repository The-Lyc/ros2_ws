import csv
from collections import defaultdict

# 分链保存数据
chain_data = defaultdict(lambda: {"RecordTime": [], "Delay": []})

# 读取原始数据
with open('jitter9_delay.csv', 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        chain = row['Chain']
        chain_data[chain]["RecordTime"].append(row['RecordTime'])
        chain_data[chain]["Delay"].append(row['Delay'])

# 写入新格式的 CSV
with open('jitter9_delay' + '_sep.csv', 'w', newline='') as f:
    writer = csv.writer(f)

    for chain in sorted(chain_data.keys()):  # 按顺序写入
        writer.writerow([chain])
        writer.writerow(["RecordTime"])
        writer.writerows([[t] for t in chain_data[chain]["RecordTime"]])
        writer.writerow(["Delay"])
        writer.writerows([[d] for d in chain_data[chain]["Delay"]])

