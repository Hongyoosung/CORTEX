import schola.generated.Points_pb2 as _Points_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

CLOSED: CommunicatorStatus
DESCRIPTOR: _descriptor.FileDescriptor
ERROR: CommunicatorStatus
GOOD: CommunicatorStatus

class EnvironmentSettings(_message.Message):
    __slots__ = ["options", "seed"]
    class OptionsEntry(_message.Message):
        __slots__ = ["key", "value"]
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    OPTIONS_FIELD_NUMBER: _ClassVar[int]
    SEED_FIELD_NUMBER: _ClassVar[int]
    options: _containers.ScalarMap[str, str]
    seed: int
    def __init__(self, seed: _Optional[int] = ..., options: _Optional[_Mapping[str, str]] = ...) -> None: ...

class EnvironmentStep(_message.Message):
    __slots__ = ["updates"]
    class UpdatesEntry(_message.Message):
        __slots__ = ["key", "value"]
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: _Points_pb2.Point
        def __init__(self, key: _Optional[str] = ..., value: _Optional[_Union[_Points_pb2.Point, _Mapping]] = ...) -> None: ...
    UPDATES_FIELD_NUMBER: _ClassVar[int]
    updates: _containers.MessageMap[str, _Points_pb2.Point]
    def __init__(self, updates: _Optional[_Mapping[str, _Points_pb2.Point]] = ...) -> None: ...

class Reset(_message.Message):
    __slots__ = ["environments"]
    class EnvironmentsEntry(_message.Message):
        __slots__ = ["key", "value"]
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: int
        value: EnvironmentSettings
        def __init__(self, key: _Optional[int] = ..., value: _Optional[_Union[EnvironmentSettings, _Mapping]] = ...) -> None: ...
    ENVIRONMENTS_FIELD_NUMBER: _ClassVar[int]
    environments: _containers.MessageMap[int, EnvironmentSettings]
    def __init__(self, environments: _Optional[_Mapping[int, EnvironmentSettings]] = ...) -> None: ...

class StateUpdate(_message.Message):
    __slots__ = ["reset", "status", "step"]
    RESET_FIELD_NUMBER: _ClassVar[int]
    STATUS_FIELD_NUMBER: _ClassVar[int]
    STEP_FIELD_NUMBER: _ClassVar[int]
    reset: Reset
    status: CommunicatorStatus
    step: Step
    def __init__(self, reset: _Optional[_Union[Reset, _Mapping]] = ..., step: _Optional[_Union[Step, _Mapping]] = ..., status: _Optional[_Union[CommunicatorStatus, str]] = ...) -> None: ...

class Step(_message.Message):
    __slots__ = ["environments"]
    ENVIRONMENTS_FIELD_NUMBER: _ClassVar[int]
    environments: _containers.RepeatedCompositeFieldContainer[EnvironmentStep]
    def __init__(self, environments: _Optional[_Iterable[_Union[EnvironmentStep, _Mapping]]] = ...) -> None: ...

class CommunicatorStatus(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = []
