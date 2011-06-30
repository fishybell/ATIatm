REM This script, using VLC 1.1.10, creates an rtsp stream server to stream audio from the microphone, if the microphone is identified by "USB PnP Sound Device"
REM You will need to change "USB PnP Sound Device" to be the correct microphone address (in VLC: Media->Open Capture Device, then Audio device name dropdown)
REM The stream address to pass to the SES will be rtsp://ip_of_this_computer:5544/

"C:\Program Files\VideoLAN\VLC\vlc.exe" dshow:// :dshow-adev="USB PnP Sound Device" --sout="#transcode{vcodec=none,acodec=mp4a,ab=128,channels=2,samplerate=44100}::rtp{sdp=rtsp://:5544/}" --no-sout-rtp-sap --no-sout-standard-sap --ttl=10 --sout-keep
