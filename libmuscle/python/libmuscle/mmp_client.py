from typing import List

import grpc
from ymmsl import Endpoint, Operator, Reference

import muscle_manager_protocol.muscle_manager_protocol_pb2 as mmp
import muscle_manager_protocol.muscle_manager_protocol_pb2_grpc as mmp_grpc

from libmuscle.endpoint import endpoint_to_grpc
from libmuscle.logging import LogMessage


CONNECTION_TIMEOUT = 300


class MMPClient():
    """The client for the MUSCLE Manager Protocol.

    This class connects to the Manager and communicates with it on \
    behalf of the rest of libmuscle.

    It manages the connection, and converts between our native types \
    and the gRPC generated types.
    """
    def __init__(self, location: str) -> None:
        """Create an MMPClient.

        Args:
            location: A connection string of the form hostname:port
        """
        channel = grpc.insecure_channel(location)
        ready = grpc.channel_ready_future(channel)
        try:
            ready.result(timeout=CONNECTION_TIMEOUT)
        except grpc.FutureTimeoutError:
            raise RuntimeError('Failed to connect to the MUSCLE manager')

        self.__client = mmp_grpc.MuscleManagerStub(channel)

    def submit_log_message(self, message: LogMessage) -> None:
        """Send a log message to the manager.

        Args:
            message: The message to send.
        """
        self.__client.SubmitLogMessage(message.to_grpc())

    def register_instance(self, name: Reference, location: str,
                          endpoints: List[Endpoint]) -> None:
        """Register a compute element instance with the manager.

        Args:
            name: Name of the instance in the simulation.
            location: String describing where the instance can be
                    reached.
            endpoints: List of endpoints of this instance.
        """
        grpc_endpoints = map(endpoint_to_grpc, endpoints)
        request = mmp.RegistrationRequest(
                instance_name=str(name),
                network_location=location,
                endpoints=grpc_endpoints)
        self.__client.RegisterInstance(request)