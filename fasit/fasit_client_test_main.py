import asyncore
import logging
import socket

FASIT_TC_IP_ADDRESS                 = 'localhost'
#FASIT_TC_IP_ADDRESS                 = '192.168.123.9'
#FASIT_TC_IP_ADDRESS                 = '157.185.52.61'
FASIT_TC_IP_PORT                    = 4000

from asynchat_fasit_client import FasitClient

logging.basicConfig(level=logging.DEBUG,
                    format='%(name)s: %(message)s',
                    )

address = (FASIT_TC_IP_ADDRESS, FASIT_TC_IP_PORT) 
ip, port = address

client0 = FasitClient(ip, port)
#client1 = FasitClient(ip, port)
#client2 = FasitClient(ip, port)

try:
    asyncore.loop(timeout = 1)
except KeyboardInterrupt:
    print "Crtl+C pressed. Shutting down."
    client0 = None
#    client1 = None
#    client2 = None


