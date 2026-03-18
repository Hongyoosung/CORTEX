# Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
"""
Exceptions for making gRPC errors more interpretable, and a context manager that automatically converts gRPC errors into our custom exceptions.
"""

import grpc
import abc
from contextlib import ContextDecorator


class ScholaException(Exception):
    """
    Base class for all exceptions in Schola.
    """

    ...


class WrappedGRPCException(ScholaException, metaclass=abc.ABCMeta):
    """
    Base class for all Schola exceptions that wrap GRPC exceptions, to add more information.
    """

    @classmethod
    @abc.abstractmethod
    def comes_from(cls, exception): ...


class NoServerError(WrappedGRPCException):
    """
    An Error that occurs when the server is not detected. Either Unreal is not running or the connection is not established.
    """

    def __init__(self, exception):
        pass

    def __str__(self):
        return (
            "No Server detected. Is Unreal Running? If it is, have you hit begin play?"
        )

    @classmethod
    def comes_from(cls, exception):
        return (
            exception.code() == grpc.StatusCode.UNAVAILABLE
            and exception.details().startswith("failed to connect to all addresses")
        )


class UnrealCrashedError(WrappedGRPCException):
    """
    An Error that occurs when Unreal has stopped responding.
    """

    def __init__(self, exception):
        pass

    def __str__(self):
        return "It looks like Unreal has stopped responding. Did you stop the running game?"

    @classmethod
    def comes_from(cls, exception):
        code_cancelled = exception.code() == grpc.StatusCode.CANCELLED
        details_cancelled = (
            exception.code() == grpc.StatusCode.UNAVAILABLE
            and exception.details() == "Cancelling all calls"
        )
        stream_cancelled = (
            exception.code() == grpc.StatusCode.UNKNOWN
            and exception.details() == "Stream removed"
        )
        return code_cancelled or details_cancelled or stream_cancelled


class MissingMethodError(WrappedGRPCException):
    """
    An Error that occurs when a method is called that doesn't exist in the Unreal environment.
    """

    def __init__(self, exception):
        pass

    def __str__(self):
        return "Expected an endpoint to exist in unreal but it doesn't. Check that your environment is configured correctly."

    @classmethod
    def comes_from(cls, exception):
        return exception.code() == grpc.StatusCode.UNIMPLEMENTED


class EnvironmentException(ScholaException):
    """
    An Exception that occurs when there is a generic issue with an environment wrapping a ScholaEnvironment
    """

    def __init__(self, message):
        super().__init__(message)


ALL_EXCEPTIONS = [NoServerError, UnrealCrashedError, MissingMethodError]


class ScholaErrorContextManager(ContextDecorator):
    """
    Wrapper for GRPC that redirects any grpc errors to our own more descriptive custom ones.
    """

    def __init__(self):
        pass

    def __enter__(self):
        pass

    def __exit__(self, exc_type, exc_value, exc_tb):
        if isinstance(exc_value, grpc.RpcError):
            # check if it matches any of our current custom exceptions
            for exception_class in ALL_EXCEPTIONS:
                if exception_class.comes_from(exc_value):
                    raise exception_class(exc_value) from exc_value
            # re-raise the current exception with false
            return False
        # return None to let the exceptions propagate on their own
        return None


class NoAgentsException(ScholaException):
    """
    An Exception that occurs when no environments are detected in Unreal Engine.

    This exception is raised when the connection to Unreal is successful, and the definition 
    contained environment(s), but some environment(s) had no agents.
    """

    def __init__(self, env_id):
        self.env_id = env_id

    def __str__(self):
        return f"Connected to Unreal successfully but Env:{self.env_id} has no agents. Please register at least one agent to each environment."


class NoEnvironmentsException(ScholaException):
    """
    An Exception that occurs when no environments are detected in Unreal Engine.

    This exception is raised when the connection to Unreal is successful but
    no environment definitions are received, indicating that no environment
    objects are present in the loaded map.
    """

    def __init__(self):
        pass

    def __str__(self):
        return "Connected to Unreal successfully but received no Environment Definitions. Check that there is an environment object in your map."
