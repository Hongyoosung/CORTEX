from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from typing import ClassVar as _ClassVar

BOOL: DType
DESCRIPTOR: _descriptor.FileDescriptor
FLOAT16: DType
FLOAT32: DType
FLOAT64: DType
INT16: DType
INT32: DType
INT64: DType
INT8: DType
UINT16: DType
UINT32: DType
UINT64: DType
UINT8: DType

class DType(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = []
