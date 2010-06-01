import asyncore
import logging
import socket
import time

FASIT_TC_IP_ADDRESS                 = 'localhost'
#FASIT_TC_IP_ADDRESS                 = '192.168.123.9'
#FASIT_TC_IP_ADDRESS                 = '157.185.52.61'
FASIT_TC_IP_PORT                    = 4000

from asynchat_fasit_client import FasitClient

logging.basicConfig(level=logging.DEBUG,format='%(name)s: %(message)s',)

address = (FASIT_TC_IP_ADDRESS, FASIT_TC_IP_PORT) 
ip, port = address
client = None

def connect_to_server():
    connected = False
    while (connected == False):
        client = None
        try:
            print "Trying to connect to server..."
            client = FasitClient(ip, port)
            connected = True
            print "...connected."
        except socket.error, (value,message): 
            connected = False
            client = None
            print "...connection refused, waiting 10 seconds."
            time.sleep(10)


def main():
    keep_going = True
    while(keep_going):
        try:
            connect_to_server()
            asyncore.loop(timeout = 1)
            print "...connection lost."
        except KeyboardInterrupt:
            print "Crtl+C pressed. Shutting down."
            client = None
            keep_going = False

if __name__ == "__main__":
    main()
