import os
import select

def main() :
    print "Opening"
    myfd = os.open("/sys/class/target/battery/level", os.O_RDONLY)
    keep_going = True
    BIG_TIMEOUT_MS=120000 ;# two minutes
    print "Polling..."

    while keep_going :
        p = select.poll()
        p.register(myfd, select.POLLERR|select.POLLPRI)
        s = p.poll(BIG_TIMEOUT_MS)

        if len(s) > 0 :
            fd, event = s.pop()
            print "...polled..."

            if fd == myfd :
                data = os.read(myfd,32)
                os.lseek(myfd,0,os.SEEK_SET)
                print "Data : %s" % data
        else :
            keep_going = False

    print "...Done"
    

if __name__ == "__main__" :
    main()

