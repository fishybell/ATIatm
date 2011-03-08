AUDIO_DIRECTORY = "/home/root/fasit/sounds/"

import fasit_audio

from threading import BoundedSemaphore
import time
import os
import random
import logging

from QThread import *

#------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------
class play_sound_thread(QThread):  
    """Class for playing sounds
    """      
    def __init__(self, number):
        QThread.__init__(self)
        logger_string = "play_sound_thread" + str(number)
        self.logger = logging.getLogger(logger_string)
        self.keep_going = True
        self.track_number = -1
        self.track_path = ""
        self.loop = 1
        self.random = False
        self.playing = False
        self.playing_semaphore = BoundedSemaphore(1)
        self.track_semaphore = BoundedSemaphore(1)
        self.audio = fasit_audio.FasitAudio()
        self.start()

    def get_track(self):
        self.track_semaphore.acquire()
        path = self.track_path
        self.track_semaphore.release() 
        return path

    def set_track(self, path):
        self.track_semaphore.acquire()
        self.track_path = path
        self.track_semaphore.release() 
        
    def is_playing(self):
        self.playing_semaphore.acquire()
        playing = self.playing
        self.playing_semaphore.release() 
        return playing
        
    def set_playing(self, playing):
        self.playing_semaphore.acquire()
        self.playing = playing
        self.playing_semaphore.release() 
        
    def record_track(self, track_number):
        if (self.is_playing() == True):
            self.logger.debug("Can't record track, player is currently playing.")
            return False
        if ((track_number < 0) or (track_number > 62)):
            self.logger.debug('Track number out of range [0 - 62]: %d', track_number)
            return False
        
        self.set_track(AUDIO_DIRECTORY+str(track_number)+".mp3")
        if (os.path.isfile(self.get_track()) == True):
            self.logger.debug('Recording file %s', self.get_track())
            self.set_playing(True)
            self.track_number = track_number
            self.loop = 0
            self.random = False
            if (self.random == True):
                self.loop = 1
            self.write("record")
            return True
        else:
            self.logger.debug('Could not find file %s', self.get_track())
            self.track_number = -1
            self.set_track("")
            self.loop = 1
            self.random = False
            return False

    def play_track(self, track_number, loop = 1, random = False):
        if (self.is_playing() == True):
            self.logger.debug("Can't play track, player is currently playing.")
            return False
        if ((track_number < 0) or (track_number > 62)):
            self.logger.debug('Track number out of range [0 - 62]: %d', track_number)
            return False
        if ((loop < 0) or (loop > 255)):
            self.logger.debug('loop out of range [0 - 255]: %d', loop)
            return False
        if ((random != True) and (random != False)):
            self.logger.debug('random out of range [True or False]: %d', random)
            return False
        
        self.set_track(AUDIO_DIRECTORY+str(track_number)+".mp3")
        if (os.path.isfile(self.get_track()) == True):
            self.logger.debug('Playing file %s', self.get_track())
            self.set_playing(True)
            self.track_number = track_number
            self.loop = loop
            self.random = random
            if (self.random == True):
                self.loop = 1
            self.write("play")
            return True
        else:
            self.logger.debug('Could not find file %s', self.get_track())
            self.track_number = -1
            self.set_track("")
            self.loop = 1
            self.random = False
            return False
        
    def run(self):
        self.logger.debug('Starting audio thread...')
        while (self.keep_going == True):
            data = self.read_in()
            #self.logger.debug('Going with %s and %s', `data`, self.get_track())
            if (data == "play"):
                path = self.get_track()
                if (path != ""):
                    self.audio.play(path, self.loop)
                    if(self.audio.poll() == True):
                        self.logger.debug('Track has stopped playing: %s', path)
                        if (self.random == True):
                            wait = random.randint(1, 10)
                            time.sleep(wait)
                            self.audio.play(path, self.loop)
                        else:
                            self.track_number = -1
                            self.set_track("")
                            self.loop = 1
                            self.random = False
            if (data == "record"):
                path = self.get_track()
                if (path != ""):
                    self.audio.record(path)
                    self.logger.debug('Track has stopped recording: %s', path)
                    self.track_number = -1
                    self.set_track("")
                    self.loop = 1
                    self.random = False
            elif data == "stop_play":
                self.random = False
                self.audio.stop()
            elif data == "stop":
                self.keep_going = False
            time.sleep(0.5)
            self.set_playing(False)


