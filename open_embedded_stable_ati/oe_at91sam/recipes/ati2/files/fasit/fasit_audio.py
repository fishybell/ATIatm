import os
import signal
import sys
import getopt
import subprocess
import time

PLAYER_COMMAND  = "/usr/bin/mplayer" 
RECORD_COMMAND  = "/usr/bin/arecord" 
CONVERT_COMMAND = "/usr/bin/lame"
REMOVE_COMMAND  = "/bin/rm"

# this is for the real HW
PLAYER_ARGS     =  " -ao alsa:device=plug=swmix "
RECORD_ARGS     =  " -Dplug:wm8731 -f cd -d 10 -c 1 "
CONVERT_ARGS    =  " -b 128 --highpass 500 "
REMOVE_ARGS     =  " -f "

#this is for testing on the desktop
#PLAYER_ARGS     =  " "

class FasitAudio:
    def __init__(self):
        self.stopped = True
        self.playproc = None
#        self.recordproc = None
#        self.recordpath = None        

    def __del__(self):
        self.playproc = None    
#        self.recordproc = None    
		
    def is_stopped(self):
        return self.stopped
        
    def play(self, sound_path, loop = 1):
        executable = PLAYER_COMMAND + " -loop " + str(loop) + PLAYER_ARGS + sound_path
        self.playproc = subprocess.Popen(executable,
                       shell = True,
                       stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT
                       )
        self.stopped = False

    def record(self, sound_path):
        self.stopped = False
        subprocess.call([RECORD_COMMAND, "-Dplug:wm8731", "-f", "cd", "-d", "10", "-c", "1", sound_path + ".wav"])
        subprocess.call([CONVERT_COMMAND, "-b", "128", "--highpass", "500", sound_path + ".wav", sound_path])
        subprocess.call([REMOVE_COMMAND, "-f", sound_path + ".wav"])
        self.stopped = True

    def stop(self):
        if (self.stopped == False):
            if self.playproc is not None:
                self.playproc.terminate()
#            if self.recordproc is not None:
#                self.recordproc.terminate()
#                self.convert()
            
    def wait(self):
        if (self.stopped == False):
            if self.playproc is not None:
                self.playproc.communicate()
                self.playproc = None
#            if self.recordproc is not None:
#                self.recordproc.communicate()
#                self.recordproc = None
#                self.convert()
            self.stopped = True
            
    def poll(self):
        if (self.stopped == False):
            if self.playproc is not None:
                if (self.playproc.poll() is None):
                    return None
                else:
                    self.playproc = None
                    self.stopped = True
                    return True
#            if self.recordproc is not None:
#                if (self.recordproc.poll() is None):
#                    return None
#                else:
#                    self.recordproc = None
#                    self.stopped = True
#                    self.convert()
#                    return True
   
   
def set_volume(volume):           
    if (volume < 0):
        volume = 0
    elif (volume > 100):
        volume = 100
    # the mixer volume level is between 0 and 151
    volume = volume * 1.51
    subprocess.call(["amixer", "set", "Speaker", str(volume)])
        
        
             

# TEST
def main():
    
    if len(sys.argv) > 1:
        print sys.argv[1]
        audio = FasitAudio()
        audio.play(sys.argv[1])
        while(audio.poll() is None):
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
