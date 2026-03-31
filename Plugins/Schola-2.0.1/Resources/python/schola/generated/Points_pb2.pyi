import schola.generated.DType_pb2 as _DType_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class BoxPoint(_message.Message):
    __slots__ = ["dtype", "shape", "values"]
    DTYPE_FIELD_NUMBER: _ClassVar[int]
    SHAPE_FIELD_NUMBER: _ClassVar[int]
    VALUES_FIELD_NUMBER: _ClassVar[int]
    dtype: _DType_pb2.DType
    shape: _containers.RepeatedScalarFieldContainer[int]
    values: _containers.RepeatedScalarFieldContainer[float]
    def __init__(self, values: _Optional[_Iterable[float]] = ..., dtype: _Optional[_Union[_DType_pb2.DType, str]] = ..., shape: _Optional[_Iterable[int]] = ...) -> None: ...

class DictPoint(_message.Message):
    __slots__ = ["values"]
    class ValuesEntry(_message.Message):
        __slots__ = ["key", "value"]
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: Point
        def __init__(self, key: _Optional[str] = ..., value: _Optional[_Union[Point, _Mapping]] = ...) -> None: ...
    VALUES_FIELD_NUMBER: _ClassVar[int]
    values: _containers.MessageMap[str, Point]
    def __init__(self, values: _Optional[_Mapping[str, Point]] = ...) -> None: ...

class DiscretePoint(_message.Message):
    __slots__ = ["value"]
    VALUE_FIELD_NUMBER: _ClassVar[int]
    value: int
    def __init__(self, value: _Optional[int] = ...) -> None: ...

class MultiBinaryPoint(_message.Message):
    __slots__ = ["values"]
    VALUES_FIELD_NUMBER: _ClassVar[int]
    values: _containers.RepeatedScalarFieldContainer[bool]
    def __init__(self, values: _Optional[_Iterable[bool]] = ...) -> None: ...

class MultiDiscretePoint(_message.Message):
    __slots__ = ["values"]
    VALUES_FIELD_NUMBER: _ClassVar[int]
    values: _containers.RepeatedScalarFieldContainer[int]
    def __init__(self, values: _Optional[_Iterable[int]] = ...) -> None: ...

class Point(_message.Message):
    __slots__ = ["box_point", "dict_point", "discrete_point", "multi_binary_point", "multi_discrete_point"]
    BOX_POINT_FIELD_NUMBER: _ClassVar[int]
    DICT_POINT_FIELD_NUMBER: _ClassVar[int]
    DISCRETE_POINT_FIELD_NUMBER: _ClassVar[int]
    MULTI_BINARY_POINT_FIELD_NUMBER: _ClassVar[int]
    MULTI_DISCRETE_POINT_FIELD_NUMBER: _ClassVar[int]
    box_point: BoxPoint
    dict_point: DictPoint
    discrete_point: DiscretePoint
    multi_binary_point: MultiBinaryPoint
    multi_discrete_point: MultiDiscretePoint
    def __init__(self, box_point: _Optional[_Union[BoxPoint, _Mapping]] = ..., discrete_point: _Optional[_Union[DiscretePoint, _Mapping]] = ..., multi_discrete_point: _Optional[_Union[MultiDiscretePoint, _Mapping]] = ..., multi_binary_point: _Optional[_Union[MultiBinaryPoint, _Mapping]] = ..., dict_point: _Optional[_Union[DictPoint, _Mapping]] = ...) -> None: ...
