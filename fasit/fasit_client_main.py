import asyncore
import logging
import socket
import time

from optparse import OptionParser
from asynchat_fasit_client import FasitClient
import fasit_packet_pd

FASIT_TC_IP_ADDRESS                 = '192.168.0.1'
FASIT_TC_IP_PORT                    = 4000

logging.basicConfig(level=logging.DEBUG,format='%(name)s: %(message)s',)
logger = logging.getLogger('fasit_client_main')

ip = FASIT_TC_IP_ADDRESS
port = FASIT_TC_IP_PORT
type = fasit_packet_pd.PD_TYPE_NONE
client = None

def connect_to_server():
    global client
    connected = False
    while (connected == False):
        client = None
        try:
            print "Trying to connect to server..."
            client = FasitClient(ip, port, type)
            connected = True
            print "...connected."
            return True
        except socket.error, (value,message): 
            connected = False
            client = None
            print "...connection refused, waiting 10 seconds."
            time.sleep(10)
        except ValueError as e:
            print e.args[0]
            return False
             


def main():
    global ip
    global port
    global type
    global client
    
    usage = "usage: %prog [options]"
    parser = OptionParser(usage = usage)
    parser.add_option("-t", "--type", dest = "target_type", help = "Set target type to TARGET_TYPE: NONE, SIT, MIT, LSAT, HSAT, MAT or SES [default: %default]", metavar = "TARGET_TYPE", default = "NONE")
    parser.add_option("-s", "--server", dest = "server_ip", help = "Set the FASIT server IP address [default: %default]", metavar = "IP_ADDRESS", default = FASIT_TC_IP_ADDRESS)
    parser.add_option("-p", "--port", dest = "server_port", help = "Set the FASIT server port number [default: %default]", metavar = "PORT_NUMBER", default = FASIT_TC_IP_PORT) 
    (options, args) = parser.parse_args()
    
    if (options.target_type == "NONE"):
        type = fasit_packet_pd.PD_TYPE_NONE
    elif (options.target_type == "SIT"):
        type = fasit_packet_pd.PD_TYPE_SIT
    elif (options.target_type == "MIT"):
        type = fasit_packet_pd.PD_TYPE_MIT
    elif (options.target_type == "LSAT"):
        type = fasit_packet_pd.PD_TYPE_SAT_LIGHT
    elif (options.target_type == "HSAT"):
        type = fasit_packet_pd.PD_TYPE_SAT_HEAVY
    elif (options.target_type == "MAT"):
        type = fasit_packet_pd.PD_TYPE_MAT
    elif (options.target_type == "SES"):
        type = fasit_packet_pd.PD_TYPE_SES
    else:
        print "\nERROR: Incorrect target type specified.\n"
        parser.print_help()
        return 0
    
    ip = options.server_ip
    port = options.server_port
   
    logger.debug("Target Type:        %s (%d)", options.target_type, type)
    logger.debug("Server IP Address:  %s", ip)
    logger.debug("Server Port:        %s", port)
    
    keep_going = True
    while(keep_going):
        try:
            if (connect_to_server() == True):
                asyncore.loop(timeout = 0.2)
                print "...connection lost."
                client = None
            else:
                client = None
                keep_going = False
        except KeyboardInterrupt:
            print "Crtl+C pressed. Shutting down."
            if (client != None):
                client.stop_threads()
            
            client = None
            keep_going = False

if __name__ == "__main__":
    main()
