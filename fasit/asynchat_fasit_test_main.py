import asyncore
import logging
import socket

from asynchat_fasit_server import FasitServer
from asynchat_fasit_client import FasitClient

logging.basicConfig(level=logging.DEBUG,
                    format='%(name)s: %(message)s',
                    )

address = ('localhost', 0) # let the kernel give us a port
server = FasitServer(address)
ip, port = server.address # find out what port we were given

client = FasitClient(ip, port)

try:
    asyncore.loop(timeout=0.500)
except KeyboardInterrupt:
    print "Crtl+C pressed. Shutting down."

