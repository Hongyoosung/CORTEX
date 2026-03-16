# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

import json
from ray.rllib.evaluation.sample_batch_builder import SampleBatchBuilder
from ray.rllib.offline.json_writer import JsonWriter


def read_expert_from_json(expert_path: str):
    # read the expert transitions from json
    obs = []
    acts = []
    combined = []
    data_path = expert_path
    with open(data_path, "r") as f:
        data = json.load(f)
        data = data["steps"]
        for step in data:
            step_number = step["stepNum"]
            step_obs = step["observations"]
            step_act = step["actions"]
            step_rews = step["rewards"] if "rewards" in step else None
            obs_concat = []
            for sensor_data in step_obs:
                sensor_name = sensor_data["interactorName"]
                sensor_value = [float(data) for data in sensor_data["value"].split(",")]
                obs_concat.extend(sensor_value)
            obs.append(obs_concat)
            step_act_concat = []
            for actuator_data in step_act:
                actuator_name = actuator_data["interactorName"]
                actuator_value = [
                    float(data) for data in actuator_data["value"].split(",")
                ]
                step_act_concat.extend(actuator_value)
            acts.append(step_act_concat)
            combined.append([obs_concat, step_act_concat])
    return combined


def convert_to_rllib_format(expert_path: str, output_path: str) -> str:
    """
    Convert the expert data to RLlib format.

    Parameters
    ----------
    expert_path : str
        Path to the original expert data.
    output_path : str
        Path to the output converted data.

    Returns
    -------
    str
        Path to the converted data.
    """

    data = read_expert_from_json(expert_path)
    batch_builder = SampleBatchBuilder()
    write_batch = JsonWriter(path=output_path)
    for i in range(len(data)):
        batch_builder.add_values(
            t=i,
            obs=data[i][0],
            actions=data[i][1],
        )
    batch = batch_builder.build_and_reset()
    write_batch.write(batch)
    return write_batch.cur_file.name
