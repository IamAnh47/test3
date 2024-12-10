import matplotlib.pyplot as plt
import re

def parse_log_file(filename):
    """ Đọc file log và trích xuất dữ liệu. """
    data = []
    current_time = -1

    with open(filename, "r") as file:
        for line in file:
            # Xác định Time slot
            time_match = re.match(r"Time slot\s+(\d+)", line)
            if time_match:
                current_time = int(time_match.group(1))
                continue

            # Xác định hành động Dispatched process
            dispatch_match = re.match(r"\s*CPU\s+(\d):\s+Dispatched process\s+(\d+)", line)
            if dispatch_match:
                cpu = int(dispatch_match.group(1))
                process = int(dispatch_match.group(2))
                data.append({"time": current_time, "cpu": cpu, "process": process})

    return data

def create_gantt_chart(data):
    """ Vẽ biểu đồ Gantt dựa trên dữ liệu đã phân tích. """
    # Tạo danh sách để lưu thời gian thực thi của mỗi tiến trình trên từng CPU
    tasks = {}
    for entry in data:
        time = entry["time"]
        cpu = entry["cpu"]
        process = entry["process"]

        if (cpu, process) not in tasks:
            tasks[(cpu, process)] = {"start": time, "end": time + 1}
        else:
            tasks[(cpu, process)]["end"] = time + 1

    # Thiết lập dữ liệu cho biểu đồ Gantt
    fig, ax = plt.subplots(figsize=(10, 6))
    colors = {}
    y_labels = []
    yticks = []
    y = 0

    for (cpu, process), times in sorted(tasks.items()):
        # Ánh xạ process ID sang tên tiến trình 
        process_name = f"p{process}s"

        # Gán màu cho mỗi tiến trình
        if process not in colors:
            colors[process] = plt.cm.tab10(process % 10)

        # Vẽ thanh ngang cho tiến trình trên CPU
        ax.barh(y, times["end"] - times["start"], left=times["start"], color=colors[process],
                edgecolor="black", label=process_name if process_name not in y_labels else "")
        y_labels.append(f"CPU {cpu}")
        yticks.append(y)
        y += 1

    # Trục và nhãn
    ax.set_yticks(yticks)
    ax.set_yticklabels(y_labels)
    ax.set_xlabel("Time Slots")
    ax.set_title("Biểu đồ Gantt về thực thi quy trình")
    ax.grid(axis="x", linestyle="--", alpha=0.6)

    # Loại bỏ trùng lặp trong legend
    handles, labels = ax.get_legend_handles_labels()
    by_label = dict(zip(labels, handles))
    ax.legend(by_label.values(), by_label.keys(), loc="upper right")

    plt.tight_layout()
    plt.show()

# Đường dẫn file log
log_file = "output_sched.txt" 

# Phân tích và vẽ biểu đồ
data = parse_log_file(log_file)
create_gantt_chart(data)
