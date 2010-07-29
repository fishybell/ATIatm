import asyncore
import logging
import socket

#FASIT_TC_IP_ADDRESS                 = 'localhost'
#FASIT_TC_IP_ADDRESS                 = '192.168.123.9'
#FASIT_TC_IP_ADDRESS                 = '157.185.52.50'
FASIT_TC_IP_ADDRESS                 = '192.168.1.100'

FASIT_TC_IP_PORT                    = 4000


import fasit_packet

from asynchat_fasit_server import FasitServer

logging.basicConfig(level=logging.DEBUG,
                    format='%(name)s: %(message)s',
                    )

address = (FASIT_TC_IP_ADDRESS, FASIT_TC_IP_PORT)
server = FasitServer(address)

try:
    asyncore.loop(timeout = 1)
except KeyboardInterrupt:
    print "Crtl+C pressed. Shutting down."
    server.close()

