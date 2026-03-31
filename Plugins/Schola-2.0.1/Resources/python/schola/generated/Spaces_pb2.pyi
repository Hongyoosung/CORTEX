import schola.generated.DType_pb2 as _DType_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class BoxSpace(_message.Message):
    __slots__ = ["dimensions", "dtype", "shape_dimensions"]
    class BoxSpaceDimension(_message.Message):
        __slots__ = ["high", "low"]
        HIGH_FIELD_NUMBER: _ClassVar[int]
        LOW_FIELD_NUMBER: _ClassVar[int]
        high: float
        low: float
        def __init__(self, low: _Optional[float] = ..., high: _Optional[float] = ...) -> None: ...
    DIMENSIONS_FIELD_NUMBER: _ClassVar[int]
    DTYPE_FIELD_NUMBER: _ClassVar[int]
    SHAPE_DIMENSIONS_FIELD_NUMBER: _ClassVar[int]
    dimensions: _containers.RepeatedCompositeFieldContainer[BoxSpace.BoxSpaceDimension]
    dtype: _DType_pb2.DType
    shape_dimensions: _containers.RepeatedScalarFieldContainer[int]
    def __init__(self, dimensions: _Optional[_Iterable[_Union[BoxSpace.BoxSpaceDimension, _Mapping]]] = ..., shape_dimensions: _Optional[_Iterable[int]] = ..., dtype: _Optional[_Union[_DType_pb2.DType, str]] = ...) -> None: ...

class DictSpace(_message.Message):
    __slots__ = ["spaces"]
    class SpacesEntry(_message.Message):
        __slots__ = ["key", "value"]
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: Space
        def __init__(self, key: _Optional[str] = ..., value: _Optional[_Union[Space, _Mapping]] = ...) -> None: ...
    SPACES_FIELD_NUMBER: _ClassVar[int]
    spaces: _containers.MessageMap[str, Space]
    def __init__(self, spaces: _Optional[_Mapping[str, Space]] = ...) -> None: ...

class DiscreteSpace(_message.Message):
    __slots__ = ["high"]
    HIGH_FIELD_NUMBER: _ClassVar[int]
    high: int
    def __init__(self, high: _Optional[int] = ...) -> None: ...

class MultiBinarySpace(_message.Message):
    __slots__ = ["shape"]
    SHAPE_FIELD_NUMBER: _ClassVar[int]
    shape: int
    def __init__(self, shape: _Optional[int] = ...) -> None: ...

class MultiDiscreteSpace(_message.Message):
    __slots__ = ["high"]
    HIGH_FIELD_NUMBER: _ClassVar[int]
    high: _containers.RepeatedScalarFieldContainer[int]
    def __init__(self, high: _Optional[_Iterable[int]] = ...) -> None: ...

class Space(_message.Message):
    __slots__ = ["box_space", "dict_space", "discrete_space", "multi_binary_space", "multi_discrete_space"]
    BOX_SPACE_FIELD_NUMBER: _ClassVar[int]
    DICT_SPACE_FIELD_NUMBER: _ClassVar[int]
    DISCRETE_SPACE_FIELD_NUMBER: _ClassVar[int]
    MULTI_BINARY_SPACE_FIELD_NUMBER: _ClassVar[int]
    MULTI_DISCRETE_SPACE_FIELD_NUMBER: _ClassVar[int]
    box_space: BoxSpace
    dict_space: DictSpace
    discrete_space: DiscreteSpace
    multi_binary_space: MultiBinarySpace
    multi_discrete_space: MultiDiscreteSpace
    def __init__(self, box_space: _Optional[_Union[BoxSpace, _Mapping]] = ..., discrete_space: _Optional[_Union[DiscreteSpace, _Mapping]] = ..., multi_discrete_space: _Optional[_Union[MultiDiscreteSpace, _Mapping]] = ..., multi_binary_space: _Optional[_Union[MultiBinarySpace, _Mapping]] = ..., dict_space: _Optional[_Union[DictSpace, _Mapping]] = ...) -> None: ...
