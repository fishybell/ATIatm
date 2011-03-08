from threading import Thread
import Queue
import logging

#------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------       
class QThread(Thread):
    """Thread class with bi-directional queues for async communications
    """
    def __init__(self):
        self.logger = logging.getLogger('QThread')
        Thread.__init__(self)
        self.daemon = True
        self.in_q = Queue.Queue() 
        self.out_q = Queue.Queue() 
        
    def run(self):
        pass

    def write(self, data):
        try:
            self.in_q.put(data, True, 2)
        except (Queue.Full):
            self.logger.debug('write to IN Q timed out, or Q was full')
    
    def read(self):
        try:
            data = self.out_q.get_nowait()
        except (Queue.Empty):
            data = None
        return data    
    
    # called within the thread
    def write_out(self, data):
        try:
            self.out_q.put(data, True, 2)
        except (Queue.Full):
            self.logger.debug('write to OUT Q timed out, or Q was full')
    
    # called within the thread
    def read_in(self):
        try:
            data = self.in_q.get_nowait()
        except (Queue.Empty):
            data = None
        return data
    
    def read_in_blocking(self, wait_seconds = 1):
        try:
            data = self.in_q.get(block = True, timeout = wait_seconds)
        except (Queue.Empty):
            data = None
        return data

