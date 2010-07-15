import os
import signal
import sys
import getopt
import subprocess
import time

PLAYER_COMMAND  = "/usr/bin/mplayer" 

# this is for the real HW
#PLAYER_ARGS     =  " -ao alsa:device=plug=swmix "

#this is for testing on the desktop
PLAYER_ARGS     =  " "

class FasitAudio:
    def __init__(self):
        self.stopped = True
        self.proc = None
        
    def __del__(self):
        self.proc = None    
		
    def is_stopped(self):
        return self.stopped
        
    def play(self, sound_path, loop = 1):
        self.executable = PLAYER_COMMAND + " -loop " + str(loop) + PLAYER_ARGS + sound_path
        self.proc = subprocess.Popen(self.executable,
                       shell = True,
                       stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE,
                       )
        self.stopped = False

    def stop(self):
        if (self.stopped == False):
            self.proc.terminate()
            
    def wait(self):
        if (self.stopped == False):
            self.proc.communicate()
            self.proc = None
            self.stopped = True
            
    def poll(self):
        if (self.stopped == False):
            if (self.proc.poll() == None):
                return None
            else:
                self.proc = None
                self.stopped = True
                return True
                

# TEST
def main():
    
    if len(sys.argv) > 1:
        print sys.argv[1]
        audio = FasitAudio()
        audio.play(sys.argv[1])
        while(audio.poll() == None):
            print "still playing..."
            time.sleep(1)
        print "done!"
#        audio.stop()
        
#    if len(sys.argv) > 2:
#        print sys.argv[2]
#        audio2 = FasitAudio()
#        audio2.play(sys.argv[2])
#    time.sleep(4)    
    
if __name__ == "__main__":
    main()
