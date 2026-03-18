import schola.generated.Points_pb2 as _Points_pb2
import schola.generated.State_pb2 as _State_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class ImitationAgentState(_message.Message):
    __slots__ = ["actions", "info", "observations", "reward", "terminated", "truncated"]
    class InfoEntry(_message.Message):
        __slots__ = ["key", "value"]
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    ACTIONS_FIELD_NUMBER: _ClassVar[int]
    INFO_FIELD_NUMBER: _ClassVar[int]
    OBSERVATIONS_FIELD_NUMBER: _ClassVar[int]
    REWARD_FIELD_NUMBER: _ClassVar[int]
    TERMINATED_FIELD_NUMBER: _ClassVar[int]
    TRUNCATED_FIELD_NUMBER: _ClassVar[int]
    actions: _Points_pb2.Point
    info: _containers.ScalarMap[str, str]
    observations: _Points_pb2.Point
    reward: float
    terminated: bool
    truncated: bool
    def __init__(self, observations: _Optional[_Union[_Points_pb2.Point, _Mapping]] = ..., reward: _Optional[float] = ..., terminated: bool = ..., truncated: bool = ..., info: _Optional[_Mapping[str, str]] = ..., actions: _Optional[_Union[_Points_pb2.Point, _Mapping]] = ...) -> None: ...

class ImitationEnvironmentState(_message.Message):
    __slots__ = ["agent_states"]
    class AgentStatesEntry(_message.Message):
        __slots__ = ["key", "value"]
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: ImitationAgentState
        def __init__(self, key: _Optional[str] = ..., value: _Optional[_Union[ImitationAgentState, _Mapping]] = ...) -> None: ...
    AGENT_STATES_FIELD_NUMBER: _ClassVar[int]
    agent_states: _containers.MessageMap[str, ImitationAgentState]
    def __init__(self, agent_states: _Optional[_Mapping[str, ImitationAgentState]] = ...) -> None: ...

class ImitationState(_message.Message):
    __slots__ = ["initial_state", "training_state"]
    INITIAL_STATE_FIELD_NUMBER: _ClassVar[int]
    TRAINING_STATE_FIELD_NUMBER: _ClassVar[int]
    initial_state: _State_pb2.InitialState
    training_state: ImitationTrainingState
    def __init__(self, training_state: _Optional[_Union[ImitationTrainingState, _Mapping]] = ..., initial_state: _Optional[_Union[_State_pb2.InitialState, _Mapping]] = ...) -> None: ...

class ImitationTrainingState(_message.Message):
    __slots__ = ["environment_states"]
    ENVIRONMENT_STATES_FIELD_NUMBER: _ClassVar[int]
    environment_states: _containers.RepeatedCompositeFieldContainer[ImitationEnvironmentState]
    def __init__(self, environment_states: _Optional[_Iterable[_Union[ImitationEnvironmentState, _Mapping]]] = ...) -> None: ...
