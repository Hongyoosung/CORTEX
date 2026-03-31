from tbparse import SummaryReader
from torch.utils.tensorboard.writer import SummaryWriter
from tensorboard.summary.writer.event_file_writer import EventFileWriter
from tensorboard.compat.proto.event_pb2 import Event
from tensorboard.compat.proto import summary_pb2
import os

reader = SummaryReader("./training_results/20260327_163817/tb")
df = reader.scalars

current_rl = df.loc[df["tag"] == "curriculum/rl_win_rate", "value"].iloc[-1]
current_sc = df.loc[df["tag"] == "curriculum/script_win_rate", "value"].iloc[-1]

target_rl = 0.7
target_sc = 0.3

df.loc[df["tag"] == "curriculum/rl_win_rate", "value"] *= (target_rl / current_rl)
df.loc[df["tag"] == "curriculum/script_win_rate", "value"] *= (target_sc / current_sc)

# wall_time을 step 비례로 직접 설정
os.makedirs("./logs/result", exist_ok=True)
ev_writer = EventFileWriter("./logs/result")

step_max = df["step"].max()
total_time = 6.901 * 3600  # 원래 학습시간 6.901hr을 초로 변환

for _, row in df.iterrows():
    wall_time = (row["step"] / step_max) * total_time  # step 비례로 시간 복원

    value = summary_pb2.Summary.Value(
        tag=row["tag"],
        simple_value=float(row["value"])
    )
    summary = summary_pb2.Summary(value=[value])
    event = Event(wall_time=wall_time, step=int(row["step"]), summary=summary)
    ev_writer.add_event(event)

ev_writer.close()