#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>

using namespace std;

#include "sit_client.h"
#include "common.h"
#include "process.h"
#include "timers.h"
#include "eeprom.h"
#include "defaults.h"


// setup calibration table
const u32 SIT_Client::cal_table[16] = {0xFFFFFFFF,333,200,125,75,60,48,37,29,22,16,11,7,4,2,1};


/***********************************************************
 *                     SIT_Client Class                     *
 ***********************************************************/
SIT_Client::SIT_Client(int fd, int tnum) : TCP_Client(fd, tnum) {
    FUNCTION_START("::SIT_Client(int fd, int tnum) : Connection(fd)");

    // we have not yet connected to SmartRange/TRACR
    ever_conn = false;
    skippedFault = 0; // haven't skipped any faults

    // connect our netlink connection
    nl_conn = NL_Conn::newConn<SIT_Conn>(this);
    if (nl_conn == NULL) {
        deleteLater();
    } else {
        reInit(); // initialize
    }
    FUNCTION_END("::SIT_Client(int fd, int tnum) : Connection(fd)");
}

SIT_Client::~SIT_Client() {
    FUNCTION_START("::~SIT_Client()");

    FUNCTION_END("::~SIT_Client()");
}

void SIT_Client::Reset() {
FUNCTION_START("::Reset()");
      int pid;
      signal(SIGCHLD, SIG_IGN);
      if (!fork()) {
        DCMSG(BLUE,"Preparing to REBOOT");
         nl_conn->~NL_Conn();
         if (fd >= 0) close(fd);
         closeListener();
         execl("/usr/bin/restart", "restart", (char *)0 );
         exit(0);
      }
      pid = getpid();
      kill(pid, SIGQUIT);
FUNCTION_END("::Reset()");
}

void SIT_Client::reInit() {
FUNCTION_START("::reInit()");
     // initialize default settings

     // initial MFS settings
     // this needs to change to something like what Nathan says here
     // but for now my kludge of 'start_config' will hold a place
     /*
      * The correct method for determining if a MFS is available is to ask the kernel using the
      * NL_C_ACCESSORY netlink command. If you set the accessory_conf "request" to 1 and
      * the "acc_type" to ACC_NES_MFS you can expect a respone with the "exists" set correctly.
      * The SIT_Conn::parseData function will need to be looking for the NL_C_ACCESSORY command.
      *
      * A clean way to do this is to send the request at the initializer of the SIT client class
      * and then remember the value from parseData.
      * To actually test this you'll need to add "insmod target_generic_output.ko has_muzzle=1"
      * to your SIT script. You may want to look at target_generic_output.c and
      * define TESTING_ON_EVAL to enable the muzzle flash to use one of the LEDs on the dev board.
      */


     sendMFSStatus = 0;
     if (start_config&PD_NES){
         doMFS(Eeprom::ReadEeprom(MFS_ACTIVATE_EXPOSE_LOC, MFS_ACTIVATE_EXPOSE_SIZE, MFS_ACTIVATE_EXPOSE),Eeprom::ReadEeprom(MFS_MODE_LOC, MFS_MODE_SIZE, MFS_MODE),Eeprom::ReadEeprom(MFS_START_DELAY_LOC, MFS_START_DELAY_SIZE, MFS_START_DELAY),Eeprom::ReadEeprom(MFS_REPEAT_DELAY_LOC, MFS_REPEAT_DELAY_SIZE, MFS_REPEAT_DELAY)); // on when fully exposed, burst, no delay, 2 seconds between bursts
     } else {
         // doMFS(0, 0, 0, 0); // Hopefully turns it of completely, or maybe we just don't call it
     }
     // TODO -- MILES SDH

     // initial hit calibration settings
     fake_sens = 1;
     lastHitCal.seperation = Eeprom::ReadEeprom(HIT_MSECS_BETWEEN_LOC, HIT_MSECS_BETWEEN_SIZE, HIT_MSECS_BETWEEN);

//if 0 -- CODE FOR SHELLY
     //char *buf = readeeprom(0x240,0x10);
     //if (sscanf(buf, "%i", &temp) == 1) {
     //   lastHitCal.seperation = temp;
     //}
//endif
     lastHitCal.sensitivity = Eeprom::ReadEeprom(HIT_DESENSITIVITY_LOC, HIT_DESENSITIVITY_SIZE, HIT_DESENSITIVITY); // fairly sensitive, but not max
     lastHitCal.blank_time = Eeprom::ReadEeprom(HIT_START_BLANKING_LOC, HIT_START_BLANKING_SIZE, HIT_START_BLANKING); // half a second blanking
     lastHitCal.enable_on = Eeprom::ReadEeprom(HIT_ENABLE_ON_LOC, HIT_ENABLE_ON_SIZE, HIT_ENABLE_ON); // hit sensor off
     lastHitCal.hits_to_kill = Eeprom::ReadEeprom(FALL_KILL_AT_X_HITS_LOC, FALL_KILL_AT_X_HITS_SIZE, FALL_KILL_AT_X_HITS); // kill on first hit
     lastHitCal.after_kill = Eeprom::ReadEeprom(FALL_AT_FALL_LOC, FALL_AT_FALL_SIZE, FALL_AT_FALL); // 0 for fall
     lastHitCal.bob_type = Eeprom::ReadEeprom(BOB_TYPE_LOC, BOB_TYPE_SIZE, BOB_TYPE); // 1 for bob at each hit until killed
     DCMSG(YELLOW,"Bob Type: %i", lastHitCal.bob_type) ;
     lastHitCal.type = Eeprom::ReadEeprom(HIT_SENSOR_TYPE_LOC, HIT_SENSOR_TYPE_SIZE, HIT_SENSOR_TYPE); // mechanical sensor
     lastHitCal.invert = Eeprom::ReadEeprom(HIT_SENSOR_INVERT_LOC, HIT_SENSOR_INVERT_SIZE, HIT_SENSOR_INVERT); // don't invert sensor input line
     lastHitCal.set = HIT_OVERWRITE_ALL;   // nothing will change without this
     nl_conn->doHitCal(lastHitCal); // tell kernel

     hits = 0;
     doHits(-1); // get correct value from kernel

     lastBatteryVal = MAX_BATTERY_VAL;
     nl_conn->doBattery(); // get a correct battery value soon
FUNCTION_END("::reInit()");
}

// fill out 2102 status message
void SIT_Client::fillStatus2102(FASIT_2102 *msg) {
    FUNCTION_START("::fillStatus2102(FASIT_2102 *msg)");

    // start with zeroes
    memset(msg, 0, sizeof(FASIT_2102));

    // fill out as response
    msg->response.rnum = resp_num;
    msg->response.rseq = resp_seq;
    resp_num = resp_seq = 0; // the next one will be unsolicited

    // exposure
    switch (exposure) {
        case 0: msg->body.exp = 0; break;
        case 1: msg->body.exp = 90; break;
        default: msg->body.exp = 45; break;
    }

    // device type
    msg->body.type = 1; // SIT. TODO -- SIT vs. SAT vs. HSAT

    //   DCMSG(YELLOW,"before  doHits(-1)   hits = %d",hits) ;    
    //doHits(-1);  // request the hit count ... or not, it isn't needed or wanted 

    // hit record
    msg->body.hit = htons(hits);     
    switch (lastHitCal.enable_on) {
        case BLANK_ALWAYS: msg->body.hit_conf.on = 0; break; // off
        case ENABLE_ALWAYS: msg->body.hit_conf.on = 1; break; // on
        case ENABLE_AT_POSITION: msg->body.hit_conf.on = 2; break; // on at
        case DISABLE_AT_POSITION: msg->body.hit_conf.on = 3; break; // off at
        case BLANK_ON_CONCEALED: msg->body.hit_conf.on = 4; break; // on
    }
    msg->body.hit_conf.react = lastHitCal.after_kill;
    msg->body.hit_conf.tokill = htons(lastHitCal.hits_to_kill);

    // use lookup table, as our sensitivity values don't match up to FASIT's
    for (int i=15; i>=0; i--) { // count backwards from most sensitive to least
        if (lastHitCal.sensitivity <= cal_table[i]) { // map our cal value to theirs
            msg->body.hit_conf.sens = htons(i); // found sensitivity value
            break; // done looking
        }
    }
    // use remembered value rather than actual value
    if (msg->body.hit_conf.sens == htons(15)) {
        msg->body.hit_conf.sens = htons(fake_sens);
    }

    msg->body.hit_conf.burst = htons(lastHitCal.seperation); // burst seperation
    msg->body.hit_conf.mode = lastHitCal.type; // single, etc.

    FUNCTION_END("::fillStatus2102(FASIT_2102 *msg)");
}

// create and send a status messsage to the FASIT server
void SIT_Client::sendStatus2102(int force) {
    FUNCTION_START("::sendStatus2102()");

    // ignore if we haven't connected yet
    if (!ever_conn) {
        FUNCTION_END("::sendStatus2102()");
        return;
    }

    FASIT_header hdr;
    FASIT_2102 msg;
    defHeader(2102, &hdr); // sets the sequence number and other data
    hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2102));

    // fill message
    fillStatus2102(&msg); // fills in status values with current values

    if (force||(memcmp(&lastMsgBody,&msg.body,sizeof(FASIT_2102b)))){
        lastMsgBody = msg.body;   // make a copy of the body of the status message for checking for changes

        DCMSG(BLUE,"Prepared to send 2102 status packet:");
        DCMSG(BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n %6d  %d.%d  %6d  %6d  %7d",htons(hdr.num),htons(hdr.icd1),htons(hdr.icd2),htonl(hdr.seq),htonl(hdr.rsrvd),htons(hdr.length));
        DCMSG(BLUE,"R-Num = %4d  R-seq-#=%4d ",htons(msg.response.rnum),htonl(msg.response.rseq));
        DCMSG(BLUE,"\t\t\t\t\t\t\tmessage body\n "\
              "PSTAT | Fault | Expos | Aspct |  Dir | Move |  Speed  | POS | Type | Hits | On/Off | React | ToKill | Sens | Mode | Burst\n"\
              "  %3d    %3d     %3d     %3d     %3d    %3d    %6.2f    %3d   %3d    %3d      %3d     %3d      %3d     %3d    %3d    %3d ",
              msg.body.pstatus,msg.body.fault,msg.body.exp,msg.body.asp,msg.body.dir,msg.body.move,msg.body.speed,msg.body.pos,msg.body.type,htons(msg.body.hit),
              msg.body.hit_conf.on,msg.body.hit_conf.react,htons(msg.body.hit_conf.tokill),htons(msg.body.hit_conf.sens),msg.body.hit_conf.mode,htons(msg.body.hit_conf.burst));

        // send
        queueMsg(&hdr, sizeof(FASIT_header));
        queueMsg(&msg, sizeof(FASIT_2102));
        finishMsg();

    } else {
        DCMSG(BLUE,"Skipped sending of a duplicate 2102 status packet:");
    }

    FUNCTION_END("::sendStatus2102()");
}

// create and store a MFS status messsage to the FASIT server
void SIT_Client::setStatus2112(int on, int mode, int idelay, int rdelay) {
    FUNCTION_START("::setStatus2112()");

    // overwrite existing values
    lastMFSBody.on = on;
    lastMFSBody.mode = mode ;
    lastMFSBody.idelay = idelay;
    lastMFSBody.rdelay = rdelay;

    // force to send?
    if (sendMFSStatus != 0) {
       sendStatus2112();
    }

    FUNCTION_END("::setStatus2112()");
}

// send stored MFS status messsage to the FASIT server
void SIT_Client::sendStatus2112() {
    FUNCTION_START("::sendStatus2112()");

    FASIT_header hdr;
    FASIT_2112 msg;
    defHeader(2112, &hdr); // sets the sequence number and other data
    hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2112));

    // set response
    // fill out as response
    msg.response.rnum = resp_num;
    msg.response.rseq = resp_seq;
    resp_num = resp_seq = 0; // the next one will be unsolicited

    // use stored values
    msg.body = lastMFSBody;

    DCMSG(BLUE,"Prepared to send 2112 status packet:");
    DCMSG(BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n %6d  %d.%d  %6d  %6d  %7d",htons(hdr.num),htons(hdr.icd1),htons(hdr.icd2),htonl(hdr.seq),htonl(hdr.rsrvd),htons(hdr.length));
    DCMSG(BLUE,"R-Num = %4d  R-seq-#=%4d ",htons(msg.response.rnum),htonl(msg.response.rseq));
    DCMSG(BLUE,"\t\t\t\t\t\t\tmessage body\n "\
          "  ON  | MODE  | idelay|rdelay \n"\
          "  %3d    %3d     %3d     %3d   ",
          msg.body.on,msg.body.mode,msg.body.idelay,msg.body.rdelay);

    // send
    queueMsg(&hdr, sizeof(FASIT_header));
    queueMsg(&msg, sizeof(FASIT_2112));
    finishMsg();

    // don't send again until requested
    sendMFSStatus = 0;

    FUNCTION_END("::sendStatus2112()");
}


// create and send a status messsage to the FASIT server
void SIT_Client::sendStatus13112(int on) {
    FUNCTION_START("::sendStatus13112()");

    FASIT_header hdr;
    FASIT_13112 msg;
    defHeader(13112, &hdr); // sets the sequence number and other data
    hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_13112));

    // set response
    // fill out as response
    msg.response.rnum = resp_num;
    msg.response.rseq = resp_seq;
    resp_num = resp_seq = 0; // the next one will be unsolicited

    if (on){
        msg.body.on = 1;
    } else {
        msg.body.on = 0;
    }
    //      didMFS(&rmsg.body.on,&rmsg.body.mode,&rmsg.body.idelay,&rmsg.body.rdelay);

    DCMSG(BLUE,"Prepared to send 13112 status packet:");
    DCMSG(BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n %6d  %d.%d  %6d  %6d  %7d",htons(hdr.num),htons(hdr.icd1),htons(hdr.icd2),htonl(hdr.seq),htonl(hdr.rsrvd),htons(hdr.length));
    DCMSG(BLUE,"R-Num = %4d  R-seq-#=%4d ",htons(msg.response.rnum),htonl(msg.response.rseq));
    DCMSG(BLUE,"\t\t\t\t\t\t\tmessage body\n "\
          "  ON  \n"\
          "  %3d   ",
          msg.body.on);

    // send
    queueMsg(&hdr, sizeof(FASIT_header));
    queueMsg(&msg, sizeof(FASIT_13112));
    finishMsg();


    FUNCTION_END("::sendStatus13112()");
}


// create and send a status messsage to the FASIT server
void SIT_Client::sendStatus14112(int on) {
    FUNCTION_START("::sendStatus14112()");

    FASIT_header hdr;
    FASIT_14112 msg;
    defHeader(14112, &hdr); // sets the sequence number and other data
    hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_14112));

    // set response
    // fill out as response
    msg.response.rnum = resp_num;
    msg.response.rseq = resp_seq;
    resp_num = resp_seq = 0; // the next one will be unsolicited

    msg.body.on = on;
    //      didMFS(&rmsg.body.on,&rmsg.body.mode,&rmsg.body.idelay,&rmsg.body.rdelay);

    DCMSG(BLUE,"Prepared to send 14112 status packet:");
    DCMSG(BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n %6d  %d.%d  %6d  %6d  %7d",htons(hdr.num),htons(hdr.icd1),htons(hdr.icd2),htonl(hdr.seq),htonl(hdr.rsrvd),htons(hdr.length));
    DCMSG(BLUE,"R-Num = %4d  R-seq-#=%4d ",htons(msg.response.rnum),htonl(msg.response.rseq));
    DCMSG(BLUE,"\t\t\t\t\t\t\tmessage body\n "\
          "  ON  \n"\
          "  %3d   ",
          msg.body.on);

    // send
    queueMsg(&hdr, sizeof(FASIT_header));
    queueMsg(&msg, sizeof(FASIT_14112));
    finishMsg();


    FUNCTION_END("::sendStatus14112()");
}

// create and send a status messsage to the FASIT server
void SIT_Client::sendStatus15112(int on) {
    FUNCTION_START("::sendStatus15112()");

    FASIT_header hdr;
    FASIT_14112 msg;
    defHeader(15112, &hdr); // sets the sequence number and other data
    hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_15112));

    // set response
    // fill out as response
    msg.response.rnum = resp_num;
    msg.response.rseq = resp_seq;
    resp_num = resp_seq = 0; // the next one will be unsolicited

    msg.body.on = on;
    //      didMFS(&rmsg.body.on,&rmsg.body.mode,&rmsg.body.idelay,&rmsg.body.rdelay);

    DCMSG(BLUE,"Prepared to send 15112 status packet:");
    DCMSG(BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n %6d  %d.%d  %6d  %6d  %7d",htons(hdr.num),htons(hdr.icd1),htons(hdr.icd2),htonl(hdr.seq),htonl(hdr.rsrvd),htons(hdr.length));
    DCMSG(BLUE,"R-Num = %4d  R-seq-#=%4d ",htons(msg.response.rnum),htonl(msg.response.rseq));
    DCMSG(BLUE,"\t\t\t\t\t\t\tmessage body\n "\
          "  ON  \n"\
          "  %3d   ",
          msg.body.on);

    // send
    queueMsg(&hdr, sizeof(FASIT_header));
    queueMsg(&msg, sizeof(FASIT_15112));
    finishMsg();


    FUNCTION_END("::sendStatus15112()");
}

/***********************************************************
 *                  FASIT Message Handlers                  *
 ***********************************************************/

//
//    Device Definition Request
//
int SIT_Client::handle_100(int start, int end) {
    FUNCTION_START("::handle_100(int start, int end)");

    // do handling of message
    IMSG("Handling 100 in SIT\n");

    // map header (no body for 100)
    FASIT_header *hdr = (FASIT_header*)(rbuf + start);
    DCMSG(RED,"************************************** Report Device Capabilities *****************************************************************************************************");
    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htons(hdr->seq),htons(hdr->rsrvd),htons(hdr->length));

    // message 100 is Device Definition Request,
    // a PD sends back a 2111
    // a pyro device sends back a 2005
    FASIT_header rhdr;
    FASIT_2111 msg;
    defHeader(2111, &rhdr); // sets the sequence number and other data
    rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2111));

    // set response
    msg.response.rnum = htons(100);
    msg.response.rseq = hdr->seq;

    // fill message
    msg.body.devid = getDevID(); // MAC address
    //retrieve the PD_NES flag from the kernel like Nate described -
    //now that I used a kludge to get those options into the device when
    //the sit_client was constructed during startup
    if (start_config&PD_NES){
        msg.body.flags = PD_NES; // TODO -- find actual capabilities from command line
    } else {
        msg.body.flags = 0;
    }
    //BDR   fasit_conn  has the command line option -S  that instaniates a
    //SIT handler.   It probably has to handle a bunch of possibilitys and
    //this part of code has to do the right thing
    // Nate said he was writing a MIT_client that handles moving targets
    // so that code would be elsewhere

    DCMSG(BLUE,"Prepared to send 2111 device capabilites message:");
    DCMSG(BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n"\
          "%4d    %d.%d     %5d    %3d     %3d",htons(rhdr.num),htons(rhdr.icd1),htons(rhdr.icd2),htons(rhdr.seq),htons(rhdr.rsrvd),htons(rhdr.length));
    DCMSG(BLUE,"\t\t\t\t\t\t\tmessage body\n Device ID (mac address backwards) | flag_bits == GPS=4,Muzzle Flash=2,MILES Shootback=1");
    DCMSG(BLUE,"0x%8.8llx           0x%2x",msg.body.devid,msg.body.flags);

    // send
    queueMsg(&rhdr, sizeof(FASIT_header));
    queueMsg(&msg, sizeof(FASIT_2111));
    finishMsg();

    // if the msg.body.flags include PD_NES then we must also generate and send a 2112, at least I thought so for a
    // while but I can't find that in the spec

    // if it is a Pyro device it responds with a 2005 device ID and
    // capabilities here (instead of or in addition to a 2111????????)

    // we were connected at some point in time
    ever_conn = true;

    // get around to skipped faults now
    if (skippedFault != 0) {
        didFailure(skippedFault);
    }

    FUNCTION_INT("::handle_100(int start, int end)", 0);
    return 0;
}

int SIT_Client::handle_2000(int start, int end) {
    FUNCTION_START("::handle_2000(int start, int end)");

    // do handling of message
    IMSG("Handling 2000 in SIT\n");

    FUNCTION_INT("::handle_2000(int start, int end)", 0);
    return 0;
}

int SIT_Client::handle_2004(int start, int end) {
    FUNCTION_START("::handle_2004(int start, int end)");

    // do handling of message
    IMSG("Handling 2004 in SIT\n");

    FUNCTION_INT("::handle_2004(int start, int end)", 0);
    return 0;
}

int SIT_Client::handle_2005(int start, int end) {
    FUNCTION_START("::handle_2005(int start, int end)");

    // do handling of message
    IMSG("Handling 2005 in SIT\n");

    FUNCTION_INT("::handle_2005(int start, int end)", 0);
    return 0;
}


int SIT_Client::handle_2006(int start, int end) {
    FUNCTION_START("::handle_2006(int start, int end)");

    // do handling of message
    IMSG("Handling 2006 in SIT\n");

    FUNCTION_INT("::handle_2006(int start, int end)", 0);
    return 0;
}

//
//  Event Command
//
int SIT_Client::handle_2100(int start, int end) {
    FUNCTION_START("::handle_2100(int start, int end)");

    int r_num, r_seq;

    // map header and body for both message and response
    FASIT_header *hdr = (FASIT_header*)(rbuf + start);
    FASIT_2100 *msg = (FASIT_2100*)(rbuf + start + sizeof(FASIT_header));


    // save response numbers
    resp_num = hdr->num; //  pulls the message number from the header  (htons was wrong here)
    resp_seq = hdr->seq;

    // Just parse out the command for now and print a pretty message
    switch (msg->cid) {
        case CID_No_Event:
            DCMSG(RED,"CID_No_Event") ; 
            break;

        case CID_Reserved01:
            DCMSG(RED,"CID_Reserved01") ;
            break;

        case CID_Status_Request:
            DCMSG(RED,"CID_Status_Request") ; 
            break;

        case CID_Expose_Request:
            DCMSG(RED,"CID_Expose_Request:  msg->exp=%d",msg->exp) ;
            break;

        case CID_Reset_Device:
            DCMSG(RED,"CID_Reset_Device  ") ;
            break;

        case CID_Move_Request:
            DCMSG(RED,"CID_Move_Request  ") ;       
            break;

        case CID_Config_Hit_Sensor:
            DCMSG(RED,"CID_Config_Hit_Sensor") ;                
            break;

        case CID_GPS_Location_Request:
            DCMSG(RED,"CID_GPS_Location_Request") ;
            break;

        case CID_Shutdown:
            DCMSG(RED,"CID_Shutdown") ;
            break;
    }

    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htons(hdr->seq),htons(hdr->rsrvd),htons(hdr->length));
    DCMSG(RED,"\t\t\t\t\t\t\tmessage body\n"\
          "C-ID | Expos | Aspct |  Dir | Move |  Speed | On/Off | Hits | React | ToKill | Sens | Mode | Burst\n"\
          "%3d    %3d     %3d     %2d    %3d    %7.2f     %4d     %2d     %3d     %3d     %3d    %3d   %5d ",
          msg->cid,msg->exp,msg->asp,msg->dir,msg->move,msg->speed,msg->on,htons(msg->hit),msg->react,htons(msg->tokill),htons(msg->sens),msg->mode,htons(msg->burst));

    // do the event that was requested
    switch (msg->cid) {
        case CID_No_Event:
            DCMSG(RED,"CID_No_Event  send 'S'uccess ack") ; 
            // send 2101 ack
            send_2101_ACK(hdr,'S');
            break;

        case CID_Reserved01:
            // send 2101 ack
            DCMSG(RED,"CID_Reserved01  send 'F'ailure ack") ;
            send_2101_ACK(hdr,'F');

            break;

        case CID_Status_Request:
            // remember and send back with all status messages
            r_num = resp_num;
            r_seq = resp_seq;
            // send 2102 status
            DCMSG(RED,"CID_Status_Request   send 2102 status") ; 
            sendStatus2102(1); // forces sending of a 2102
            // send 2112 Muzzle Flash status if supported   
            if (start_config&PD_NES){
                resp_num = r_num;
                resp_seq = r_seq;
                DCMSG(RED,"we also seem to have a MFS Muzzle Flash Simulator - send 2112 status") ; 
                sendStatus2112(); // forces sending of a 2112
            }

            // force sending of all messages now (will block until they are sent)
//            forceQueueDump();
            break;

        case CID_Expose_Request:
            DCMSG(RED,"CID_Expose_Request  send 'S'uccess ack.   msg->exp=%d",msg->exp) ;       
            // send 2101 ack  (2102's will be generated at start and stop of actuator)
            send_2101_ACK(hdr,'S');    // TRACR Cert complains if these are not there

            switch (msg->exp) {
                case 0:
                    doConceal();
                    break;
                case 0x2D:
                    break;
                case 0x5A:
                    doExpose();
                    break;
            }
            break;

        case CID_Reset_Device:
            // send 2101 ack
            DCMSG(RED,"CID_Reset_Device  send 'S'uccess ack.   set lastHitCal.* to defaults") ;
            send_2101_ACK(hdr,'S');
            // also supposed to reset all values to the 'initial exercise step value'
            //  which I am not sure if it is different than ordinary inital values 
            Reset();
            break;

        case CID_Move_Request:
            // send 2101 ack  (2102's will be generated at start and stop of actuator)
            DCMSG(RED,"CID_Move_Request  send 'S'uccess ack.   TODO send the move to the kernel?") ;        
            send_2101_ACK(hdr,'S');
            break;

        case CID_Config_Hit_Sensor:
            DCMSG(RED,"CID_Config_Hit_Sensor  send 'S'uccess ack.   TODO add sending a 2102?") ;
            //      actually Riptide says that the FASIT spec is wrong and should not send an ACK here       
            //          send_2101_ACK(hdr,'S'); // FASIT Cert seems to complain about this ACK

            // send 2102 status - after doing what was commanded
            // which is setting the values in the hit_calibration structure
            // uses the lastHitCal so what we set is recorded
            // there are fields that don't match up

            // TODO I believe a 2100 config hit sensor is supposed to set the hit count
            switch (msg->on) {
                case 0: lastHitCal.enable_on = BLANK_ALWAYS; break; // hit sensor Off
                case 1: lastHitCal.enable_on = ENABLE_ALWAYS; break; // hit sensor On
                case 2: lastHitCal.enable_on = ENABLE_AT_POSITION; break; // hit sensor On at Position
                case 3: lastHitCal.enable_on = DISABLE_AT_POSITION; break; // hit sensor Off at Position
                case 4: lastHitCal.enable_on = BLANK_ON_CONCEALED; break; // hit sensor On when exposed
            }
            if (htons(msg->burst)) lastHitCal.seperation = htons(msg->burst);      // spec says we only set if non-Zero
            if (htons(msg->sens)) {
                if (htons(msg->sens) > 15) {
                    lastHitCal.sensitivity = cal_table[15];
                } else {
                    lastHitCal.sensitivity = cal_table[htons(msg->sens)];
                }
                // remember told value for later
                fake_sens = htons(msg->sens);
            }
            if (htons(msg->tokill))  lastHitCal.hits_to_kill = htons(msg->tokill); 
            lastHitCal.after_kill = msg->react;    // 0 for stay down
            lastHitCal.type = msg->mode;           // mechanical sensor
            lastHitCal.set = HIT_OVERWRITE_ALL;    // nothing will change without this
            doHitCal(lastHitCal); // tell kernel by calling SIT_Clients version of doHitCal
            DCMSG(RED,"calling doHitCal after setting values") ;        
            // send 2102 status or change the hit count (which will send the 2102 later)
            if (hits == htons(msg->hit)) {
                sendStatus2102(1);  // sends a 2102 as we won't if we didn't change the the hit count
                //sendStatus2102(0);  // sends a 2102 only if we changed the the hit calibration
                DCMSG(RED,"We will send 2102 status in response to the config hit sensor command"); 
            } else {
                doHits(htons(msg->hit));    // set hit count to something other than zero
                DCMSG(RED,"after doHits(%d) ",htons(msg->hit)) ;
            }

            break;

        case CID_GPS_Location_Request:
            DCMSG(RED,"CID_GPS_Location_Request  send 'F'ailure ack  - because we don't support it") ;
            send_2101_ACK(hdr,'F');

            // send 2113 GPS Location
            break;

         case CID_Stop:
            doStop();
            break;
        case CID_Shutdown:
            DCMSG(RED,"CID_Shutdown...shutting down") ; 
            doShutdown();
            break;
        case CID_Sleep:
            DCMSG(RED,"CID_Sleep...sleeping") ; 
            doSleep();
            break;
        case CID_Wake:
            DCMSG(RED,"CID_Wake...waking") ; 
            doWake();
            break;
    }

    FUNCTION_INT("::handle_2100(int start, int end)", 0);
    return 0;
}

int SIT_Client::handle_2101(int start, int end) {
    FUNCTION_START("::handle_2101(int start, int end)");

    // do handling of message
    IMSG("Handling 2101 in SIT\n");

    FUNCTION_INT("::handle_2101(int start, int end)", 0);
    return 0;
}


int SIT_Client::handle_2102(int start, int end) {
    FUNCTION_START("::handle_2102(int start, int end)");

    // do handling of message
    IMSG("Handling 2102 in SIT\n");

    FUNCTION_INT("::handle_2102(int start, int end)", 0);
    return 0;
}

//
//  Configure Muzzle Flash
//     We respond with a 2112 to indicate the updated status if we support Muzzle flash,
//     If we don't support Mozzle flash we are to repsond with a negative command acknowledgement
//     (acknowledge response = 'F')   Not sure how to send that right now
//
int SIT_Client::handle_2110(int start, int end) {
    FUNCTION_START("::handle_2110(int start, int end)");

    // do handling of message
    IMSG("Handling 2110 in SIT\n");

    // map header and body for both message and response
    FASIT_header *hdr = (FASIT_header*)(rbuf + start);
    FASIT_2110 *msg = (FASIT_2110*)(rbuf + start + sizeof(FASIT_header));

    // save response numbers
    resp_num = hdr->num; //  pulls the message number from the header  (htons was wrong here)
    resp_seq = hdr->seq;

    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htons(hdr->seq),htons(hdr->rsrvd),htons(hdr->length));
    DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nOn/Off | Mode | I-Delay | R-Delay\n%7d  %5d  %8d  %8d",
          msg->on,msg->mode,msg->idelay,msg->rdelay);

    // check to see if we have muzzle flash capability -
    if (start_config&PD_NES){

        doMFS(msg->on,msg->mode,msg->idelay,msg->rdelay);

        // when the didMFS happens fill in the 2112, force a 2112 message to be sent
        sendMFSStatus = 1; // force
    } else {
        send_2101_ACK(hdr,'F');   // no muzzle flash capability, so send a negative ack
    }

    FUNCTION_INT("::handle_2110(int start, int end)", 0);
    return 0;
}

int SIT_Client::handle_2111(int start, int end) {
    FUNCTION_START("::handle_2111(int start, int end)");

    // do handling of message
    IMSG("Handling 2111 in SIT\n");

    FUNCTION_INT("::handle_2111(int start, int end)", 0);
    return 0;
}


int SIT_Client::handle_2112(int start, int end) {
    FUNCTION_START("::handle_2112(int start, int end)");

    // do handling of message
    IMSG("Handling 2112 in SIT\n");

    FUNCTION_INT("::handle_2112(int start, int end)", 0);
    return 0;
}

int SIT_Client::handle_2113(int start, int end) {
    FUNCTION_START("::handle_2113(int start, int end)");

    // do handling of message
    IMSG("Handling 2113 in SIT\n");

    FUNCTION_INT("::handle_2113(int start, int end)", 0);
    return 0;
}

int SIT_Client::handle_2114(int start, int end) {
    FUNCTION_START("::handle_2114(int start, int end)");

    // do handling of message
    IMSG("Handling 2114 in SIT\n");

    // map header and body for both message and response
    FASIT_header rhdr;
    FASIT_2115 rmsg;
    FASIT_header *hdr = (FASIT_header*)(rbuf + start);
    FASIT_2114 *msg = (FASIT_2114*)(rbuf + start + sizeof(FASIT_header));

    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htons(hdr->seq),htons(hdr->rsrvd),htons(hdr->length));
    DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nCode | Ammo | Player | Delay \n%5d %5d %7d %7d",
          msg->code, msg->ammo, msg->player, msg->delay);

    // check to see if we have the Night Effects Simulator
    if (start_config&PD_NES){
        doMSDH(msg->code, msg->ammo, msg->player, msg->delay);

        // then respond with a 2115 of it's status
        defHeader(2115, &rhdr); // sets the sequence number and other data
        rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2115));

        // set response
        rmsg.response.rnum = htons(hdr->num);   //  pulls the message number from the header
        rmsg.response.rseq = hdr->seq;      


    } else {
        send_2101_ACK(hdr,'F'); // no muzzle flash capability, so send a negative ack
    }

    FUNCTION_INT("::handle_2114(int start, int end)", 0);
    return 0;
}

int SIT_Client::handle_2115(int start, int end) {
    FUNCTION_START("::handle_2115(int start, int end)");

    // do handling of message
    IMSG("Handling 2115 in SIT\n");

    FUNCTION_INT("::handle_2115(int start, int end)", 0);
    return 0;
}
//
//  Configure Moon glow 
//     We respond with a 13112 to indicate the updated status if we support Moon Glow,
//     If we don't support Mozzle flash we are to repsond with a negative command acknowledgement
//     (acknowledge response = 'F')   Not sure how to send that right now
//
int SIT_Client::handle_13110(int start, int end) {
    FUNCTION_START("::handle_13110(int start, int end)");

    // do handling of message
    IMSG("Handling 13110 in SIT\n");

    // map header and body for both message and response
    FASIT_header rhdr;
    FASIT_13112 rmsg;
    FASIT_header *hdr = (FASIT_header*)(rbuf + start);
    FASIT_13110 *msg = (FASIT_13110*)(rbuf + start + sizeof(FASIT_header));

    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htons(hdr->seq),htons(hdr->rsrvd),htons(hdr->length));
    DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nOn/Off \n%7d",
          msg->on);

    // check to see if we have the Night Effects Simulator
    if (start_config&PD_NES){

        doMGL(msg->on);

        // then respond with a 13112 of it's status
        defHeader(13112, &rhdr); // sets the sequence number and other data
        rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_13112));

        // set response
        rmsg.response.rnum = htons(hdr->num);   //  pulls the message number from the header
        rmsg.response.rseq = hdr->seq;      


    } else {
        send_2101_ACK(hdr,'F'); // no muzzle flash capability, so send a negative ack
    }

    FUNCTION_INT("::handle_13110(int start, int end)", 0) ;
    return 0;
}

int SIT_Client::handle_13112(int start, int end) {
    FUNCTION_START("::handle_13112(int start, int end)")

    // do handling of message
            IMSG("Handling 13112 in SIT \n");

    FUNCTION_INT("::handle_13112(int start, int end)", 0)
            return 0;
}

//
//  Configure Muzzle Flash
//     We respond with a 14112 to indicate the updated status if we support Muzzle flash,
//     If we don't support Mozzle flash we are to repsond with a negative command acknowledgement
//     (acknowledge response = 'F')   Not sure how to send that right now
//
int SIT_Client::handle_14110(int start, int end) {
    FUNCTION_START("::handle_14110(int start, int end)");

    // do handling of message
    IMSG("Handling 14110 in SIT\n");

    // map header and body for both message and response
    FASIT_header rhdr;
    FASIT_14112 rmsg;
    FASIT_header *hdr = (FASIT_header*)(rbuf + start);
    FASIT_14110 *msg = (FASIT_14110*)(rbuf + start + sizeof(FASIT_header));

    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htons(hdr->seq),htons(hdr->rsrvd),htons(hdr->length));
    DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nOn/Off \n%7d",
          msg->on);

    // check to see if we have the Night Effects Simulator
    if (start_config&PD_NES) {

        doPHI(msg->on);

        // then respond with a 14112 of it's status
        defHeader(14112, &rhdr); // sets the sequence number and other data
        rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_14112));

        // set response
        rmsg.response.rnum = htons(hdr->num);   //  pulls the message number from the header
        rmsg.response.rseq = hdr->seq;      


    } else {
        send_2101_ACK(hdr,'F'); // no muzzle flash capability, so send a negative ack
    }
    FUNCTION_INT("::handle_14110(int start, int end)", 0);
    return 0;
}


int SIT_Client::handle_14112(int start, int end) {
    FUNCTION_START("::handle_14112(int start, int end)");

    // do handling of message
    IMSG("Handling 14112 in SIT\n");

    FUNCTION_INT("::handle_14112(int start, int end)", 0);
    return 0;
}

//
//  Configure Thermals 
//     We respond with a 15112 to indicate the updated status if we support Thermals,
//     If we don't support Mozzle flash we are to repsond with a negative command acknowledgement
//     (acknowledge response = 'F')   Not sure how to send that right now
//
int SIT_Client::handle_15110(int start, int end) {
    FUNCTION_START("::handle_15110(int start, int end)");

    // do handling of message
    IMSG("Handling 15110 in SIT\n");

    // map header and body for both message and response
    FASIT_header rhdr;
    FASIT_15112 rmsg;
    FASIT_header *hdr = (FASIT_header*)(rbuf + start);
    FASIT_15110 *msg = (FASIT_15110*)(rbuf + start + sizeof(FASIT_header));

    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htons(hdr->seq),htons(hdr->rsrvd),htons(hdr->length));
    DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nNA/On/Off \n%7d",
          msg->on);

    // check to see if we have the Night Effects Simulator
    if (start_config&PD_NES){
        doTherm(msg->on);

        // then respond with a 13112 of it's status
        defHeader(15112, &rhdr); // sets the sequence number and other data
        rhdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_15112));

        // set response
        rmsg.response.rnum = htons(hdr->num);   //  pulls the message number from the header
        rmsg.response.rseq = hdr->seq;      


    } else {
        send_2101_ACK(hdr,'F'); // no muzzle flash capability, so send a negative ack
    }

    FUNCTION_INT("::handle_15110(int start, int end)", 0) ;
    return 0;
}

int SIT_Client::handle_15112(int start, int end) {
    FUNCTION_START("::handle_15112(int start, int end)");

    // do handling of message
    IMSG("Handling 15112 in SIT\n");

    FUNCTION_INT("::handle_15112(int start, int end)", 0);
    return 0;
}


int SIT_Client::handle_14200(int start, int end) {
    FUNCTION_START("::handle_14200(int start, int end)");

    // do handling of message
    IMSG("Handling 14200 in SIT\n");

    // map header and body for both message and response
    FASIT_header *hdr = (FASIT_header*)(rbuf + start);
    FASIT_14200 *msg = (FASIT_14200*)(rbuf + start + sizeof(FASIT_header));

    DCMSG(RED,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n%6d  %d.%d  %6d  %6d  %7d",htons(hdr->num),htons(hdr->icd1),htons(hdr->icd2),htons(hdr->seq),htons(hdr->rsrvd),htons(hdr->length));
    DCMSG(RED,"\t\t\t\t\t\t\tmessage body\nBlank \n%7d",
          htons(msg->blank));

    // set hit sensor blanking
    doBlank(htons(msg->blank));

    FUNCTION_INT("::handle_14200(int start, int end)", 0);
    return 0;
}

int SIT_Client::handle_14400(int start, int end) {
    FUNCTION_START("::handle_14400(int start, int end)");

    // do handling of message
    IMSG("Handling 14400 in SIT\n");

    FUNCTION_INT("::handle_14400(int start, int end)", 0);
    return 0;
}

int SIT_Client::handle_14401(int start, int end) {
    FUNCTION_START("::handle_14401(int start, int end)");

    // do handling of message
    IMSG("Handling 14401 in SIT\n");

    FUNCTION_INT("::handle_14401(int start, int end)", 0);
    return 0;
}

/***********************************************************
 *                    Basic SIT Commands                    *
 ***********************************************************/
// experienced failure "type"
void SIT_Client::didFailure(int type) {
    FUNCTION_START("::didFailure(int type)");

    // ignore if we haven't connected yet
    if (!ever_conn) {
        skippedFault = type; // send after connection
        FUNCTION_END("::didFailure(int type)")
        return;
    }

    FASIT_header hdr;
    FASIT_2102 msg;
    DCMSG(BLUE,"Prepared to send 2102 failure packet: %i", type);
    defHeader(2102, &hdr); // sets the sequence number and other data
    hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2102));

    // fill message
    fillStatus2102(&msg); // fills in status values with current values

    // set fault
    msg.body.fault = htons(type);

    // send
    queueMsg(&hdr, sizeof(FASIT_header));
    queueMsg(&msg, sizeof(FASIT_2102));
    finishMsg();

    FUNCTION_END("::didFailure(int type)");
}

// change position to conceal
void SIT_Client::doConceal() {
    FUNCTION_START("::doConceal()");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doConceal();
    }
    FUNCTION_END("::doConceal()");
}

// position changed to conceal
void SIT_Client::didConceal() {
    FUNCTION_START("::didConceal()");

    // remember that we are concealed
    exposure = CONCEAL;

    // send status message to FASIT server
    DCOLOR(RED) ; // change color
    sendStatus2102(0);
    DCOLOR(black);

    FUNCTION_END("::didConceal()");
}

// change position to expose
void SIT_Client::doExpose() {
    FUNCTION_START("::doExpose()");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doExpose();
    }
    FUNCTION_END("::doExpose()");
}

// position changed to expose
void SIT_Client::didExpose() {
    FUNCTION_START("::didExpose()");

    // remember that we are exposed
    exposure = EXPOSE;

    DCOLOR(RED) ; // change color
    // send status message to FASIT server
    sendStatus2102(0);
    DCOLOR(black);

    FUNCTION_END("::didExpose()");
}

// position changed to moving
void SIT_Client::didMoving() {
    FUNCTION_START("::didMoving()");

    // remember that we are moving
    exposure = LIFTING;

    // send status message to FASIT server
    sendStatus2102(0);

    FUNCTION_END("::didMoving()");
}

// shutdown device
void SIT_Client::doShutdown() {
    FUNCTION_START("::doShutdown()");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doShutdown();
    }
    FUNCTION_END("::doShutdown()");
}

// sleep device
void SIT_Client::doSleep() {
    FUNCTION_START("::doSleep()");
    // pass directly to kernel for actual action
    if (nl_conn != NULL) {
        nl_conn->doSleep();
    }
    FUNCTION_END("::doSleep()");
}

// wake device
void SIT_Client::doWake() {
    FUNCTION_START("::doWake()");
    // pass directly to kernel for actual action
    if (nl_conn != NULL) {
        nl_conn->doWake();
    }
    FUNCTION_END("::doWake()");
}

// retrieve battery value
void SIT_Client::doBattery() {
    FUNCTION_START("::doBattery()");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doBattery();
    }
    FUNCTION_END("::doBattery()");
}

// current battery value
void SIT_Client::didBattery(int val) {
    FUNCTION_START("::didBattery(int val)");

    // if we're low we'll need to tell userspace
    if (val <= FAILURE_BATTERY_VAL) {
        didFailure(ERR_critical_battery);
    } else if (val <= MIN_BATTERY_VAL) {
        didFailure(ERR_low_battery);
    }

    // save the information for the next time
    lastBatteryVal = val;

    FUNCTION_END("::didBattery(int val)");
}

// current fault value
void SIT_Client::didFault(int val) {
    FUNCTION_START("::didFault(int val)");

    didFailure(val);

    FUNCTION_END("::didFault(int val)");
}

// immediate stop (stops accessories as well)
void SIT_Client::doStop() {
    FUNCTION_START("::doStop()");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doStop();
    }
    FUNCTION_END("::doStop()");
}

// received immediate stop response
void SIT_Client::didStop() {
    FUNCTION_START("::didStop()");

    // notify the front-end of the emergency stop
    didFailure(ERR_emergency_stop);

    FUNCTION_END("::didStop()");
}

// change hit calibration data
void SIT_Client::doHitCal(struct hit_calibration hit_c) {
    FUNCTION_START("::doHitCal(struct hit_calibration hit_c)");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doHitCal(hit_c);
    }
    FUNCTION_END("::doHitCal(struct hit_calibration hit_c)");
}

// current hit calibration data
void SIT_Client::didHitCal(struct hit_calibration hit_c) {
    FUNCTION_START("::didHitCal(struct hit_calibration hit_c)");
    // ignore?
    FUNCTION_END("::didHitCal(struct hit_calibration hit_c)");
}

// get last remembered hit calibration data
void SIT_Client::getHitCal(struct hit_calibration *hit_c) {
    FUNCTION_START("::getHitCal(struct hit_calibration *hit_c)");
    // give the previous hit calibration data
    *hit_c = lastHitCal;
    FUNCTION_END("::getHitCal(struct hit_calibration *hit_c)");
}

// get last remembered accessory config
void SIT_Client::getAcc_C(struct accessory_conf *acc_c) {
    FUNCTION_START("::getAcc_C((struct accessory_conf *acc_c)");
    // give the previous accessory config data
    *acc_c = acc_conf;
    FUNCTION_END("::getAcc_C((struct accessory_conf *acc_c)");
}

// change received hits to "num" (usually to 0 for reset)
void SIT_Client::doHits(int num) {

    DCOLOR(CYAN) ; // change color
    FUNCTION_START("SIT_Client::doHits(int num)");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doHits(num);
    }
    FUNCTION_END("SIT_Client::doHits(int num)") ;
    DCOLOR(black) ; // change color
}

// received "num" hits
void SIT_Client::didHits(int num) {
    FUNCTION_START("SIT_Client::didHits(int num)");

    // are we different than our remember value?
    if (hits != num) {
        // remember new value
        hits = num;

        // if we have a hit count, send it
        sendStatus2102(0); // send status message to FASIT server
    }

    FUNCTION_END("SIT_Client::didHits(int num)");

}

// change MSDH data
void SIT_Client::doMSDH(int code, int ammo, int player, int delay) {
    FUNCTION_START("::doMSDH(int code, int ammo, int player, int delay)");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doMSDH(code, ammo, player, delay);
    }
    FUNCTION_END("::doMSDH(int code, int ammo, int player, int delay)");
}

// current MSDH data
void SIT_Client::didMSDH(int code, int ammo, int player, int delay) {
    FUNCTION_START("::didMSDH(int code, int ammo, int player, int delay)");
    FASIT_header hdr;
    FASIT_2115 msg;
    defHeader(2115, &hdr); // sets the sequence number and other data
    hdr.length = htons(sizeof(FASIT_header) + sizeof(FASIT_2115));

    // fill out response
    msg.response = getResponse(2114); // Configure MILES Shootback Command

    // fill message
    msg.body.code = code & 0xFF;
    msg.body.ammo = ammo & 0xFF;
    msg.body.player = htons(player & 0xFFFF);
    msg.body.delay = delay & 0xFF;

    DCMSG(BLUE,"Prepared to send 2115 status packet:");
    DCMSG(BLUE,"header\nM-Num | ICD-v | seq-# | rsrvd | length\n %6d  %d.%d  %6d  %6d  %7d",htons(hdr.num),htons(hdr.icd1),htons(hdr.icd2),htonl(hdr.seq),htonl(hdr.rsrvd),htons(hdr.length));
    DCMSG(BLUE,"R-Num = %4d  R-seq-#=%4d ",htons(msg.response.rnum),htonl(msg.response.rseq));
    DCMSG(BLUE,"\t\t\t\t\t\t\tmessage body\n "\
          "  Code | Ammo | Player | Delay  \n"\
          "  %5d %5d %7d %7d   ",
          msg.body.code, msg.body.ammo, msg.body.player, msg.body.delay);

    // send
    queueMsg(&hdr, sizeof(FASIT_header));
    queueMsg(&msg, sizeof(FASIT_2115));
    finishMsg();

    FUNCTION_END("::didMSDH(int code, int ammo, int player, int delay)");
}

// change MFS data
void SIT_Client::doMFS(int on, int mode, int idelay, int rdelay) {
    FUNCTION_START("::doMFS(int on, int mode, int idelay, int rdelay)");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doMFS(on, mode, idelay, rdelay);
    }
    FUNCTION_END("::doMFS(int on, int mode, int idelay, int rdelay)");
}

// current MFS data
void SIT_Client::didMFS(int exists,int on, int mode, int idelay, int rdelay) {
    FUNCTION_START("::didMFS(int exists,int on, int mode, int idelay, int rdelay) ");
    // send status message to FASIT server
    DCOLOR(RED) ; // change color
    if (exists) {
        setStatus2112(on, mode, idelay, rdelay);
    } else {
//        send_2101_ACK(hdr,'F'); // probably not really right
    }

    // there needs to be some actual code here if it is going to function
    FUNCTION_END("::didMFS(int *on, int *mode, int *idelay, int *rdelay)");
}

// change MGL data
void SIT_Client::doMGL(int on) {
    FUNCTION_START("::doMGL(int on)");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doMGL(on);
    }
    FUNCTION_END("::doMGL(int on)");
}


void SIT_Client::didMGL(int exists,int on) {
    FUNCTION_START("::didMGL(int exists,int on) ");
    // send status message to FASIT server
    DCOLOR(RED) ; // change color
    if (exists) {
        sendStatus13112(on);
    } else {
//        send_2101_ACK(hdr,'F'); // probably not really right
    }

    // there needs to be some actual code here if it is going to function
    FUNCTION_END("::didMGL");
}


void SIT_Client::doPHI(int on) {
    FUNCTION_START("::doPHI(int on)");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doPHI(on);
    }
    FUNCTION_END("::doPHI(int on)");
}


void SIT_Client::didPHI(int exists,int on) {
    FUNCTION_START("::didPHI(int exists,int on) ");
    // send status message to FASIT server
    DCOLOR(RED) ; // change color
    if (exists) {
	if (on) {
	    sendStatus14112(1);
	} else {
	    sendStatus14112(0);
	}
    } else {
//        send_2101_ACK(hdr,'F'); // probably not really right
    }

    // there needs to be some actual code here if it is going to function
    FUNCTION_END("::didPHI");
}

// change Thermal data
void SIT_Client::doTherm(int on) {
    FUNCTION_START("::doTherm(int on)");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doTherm(on);
    }
    FUNCTION_END("::doTherm(int on)");
}


void SIT_Client::didTherm(int exists,int on) {
    FUNCTION_START("::didTherm(int exists,int on) ");
    // send status message to FASIT server
    DCOLOR(RED) ; // change color
    if (exists) {
        if (on == 2) {
            sendStatus15112(2);
        } else if (on == 1) {
	        sendStatus15112(1);
	    } else if (on == 0) {
	        sendStatus15112(0);
	    } else {
//        send_2101_ACK(hdr,'F'); // probably not really right
        }
    }

    // there needs to be some actual code here if it is going to function
    FUNCTION_END("::didTherm");
}

void SIT_Client::doBlank(int blank) {
    FUNCTION_START("::doBlank(int blank)");
    lastHitCal.blank_time = blank;
    lastHitCal.set = HIT_OVERWRITE_ALL;    // nothing will change without this
    doHitCal(lastHitCal); // tell kernel by calling SIT_Clients version of doHitCal
    FUNCTION_END("::doBlank(int blank)");
}

// retrieve gps data
void SIT_Client::doGPS() {
    FUNCTION_START("::doGPS()");
    // pass directly to kernel for actual action
    if (hasPair()) {
        nl_conn->doGPS();
    }
    FUNCTION_END("::doGPS()");
}

// current gps data
void SIT_Client::didGPS(struct gps_conf gpc_c) {
    FUNCTION_START("::didGPS(struct gps_conf gpc_c)");
    FUNCTION_END("::didGPS(struct gps_conf gpc_c)");
}


/***********************************************************
 *                      SIT_Conn Class                      *
 ***********************************************************/
SIT_Conn::SIT_Conn(struct nl_handle *handle, SIT_Client *client, int family) : NL_Conn(handle, client, family) {
    FUNCTION_START("::SIT_Conn(struct nl_handle *handle, TCP_Client *client, int family) : NL_Conn(handle, client, family)");

    sit_client = client;

    FUNCTION_END("::SIT_Conn(struct nl_handle *handle, TCP_Client *client, int family) : NL_Conn(handle, client, family)");
}

SIT_Conn::~SIT_Conn() {
    FUNCTION_START("::~SIT_Conn()");

    FUNCTION_END("::~SIT_Conn()");
}

int SIT_Conn::parseData(struct nl_msg *msg) {
    FUNCTION_START("SIT_Conn::parseData(struct nl_msg *msg)");
    struct nlattr *attrs[NL_A_MAX+1];
    struct nlmsghdr *nlh = nlmsg_hdr(msg);
    struct genlmsghdr *ghdr = static_cast<genlmsghdr*>(nlmsg_data(nlh));
    int *data;

    DCMSG(GREEN,"parseData switch on netlink command enum of %d",ghdr->cmd) ;
    // parse message and call individual commands as needed
    switch (ghdr->cmd) {
        case NL_C_FAILURE:
            genlmsg_parse(nlh, 0, attrs, GEN_STRING_A_MAX, generic_string_policy);
            DCMSG(RED,"parseData case NL_C_FAILURE: attrs = 0x%x",attrs[GEN_STRING_A_MSG]) ;

            // TODO -- failure messages need decodable data
            if (attrs[GEN_STRING_A_MSG]) {
                char *data = nla_get_string(attrs[GEN_STRING_A_MSG]);
                IERROR("netlink failure attribute: %s\n", data)
            }

            break;
        case NL_C_EXPOSE:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);
            data=(int *)nla_data(attrs[GEN_INT8_A_MSG]);
            {
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);

                DCMSG(green,"parseData case NL_C_EXPOSE: attrs = 0x%x, nla_data (0x%x)  =%d === %d ",attrs[GEN_INT8_A_MSG],data,*data,value);
            }
            if (attrs[GEN_INT8_A_MSG]) {
                // received change in exposure
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                switch (value) {
                    case 0:
                        sit_client->didConceal(); // tell client
                        break;
                    case 1:
                        sit_client->didExpose(); // tell client
                        break;
                    default:
                        sit_client->didMoving(); // tell client
                        break;
                }
            }
            break;
        case NL_C_BATTERY:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);
            data=(int *)nla_data(attrs[GEN_INT8_A_MSG]);
            DCMSG(BLUE,"parseData case NL_C_BATTERY: attrs = 0x%x, nla_data (0x%x)  =%d ",attrs[GEN_INT8_A_MSG],data,*data);

            if (attrs[GEN_INT8_A_MSG]) {
                // received change in battery value
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                sit_client->didBattery(value); // tell client
            }
            break;
        case NL_C_FAULT:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);
            data=(int *)nla_data(attrs[GEN_INT8_A_MSG]);
            DCMSG(BLUE,"parseData case NL_C_FAULT: attrs = 0x%x, nla_data (0x%x)  =%d ",attrs[GEN_INT8_A_MSG],data,*data);

            if (attrs[GEN_INT8_A_MSG]) {
                // received change in battery value
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                sit_client->didFault(value); // tell client
            }
            break;
        case NL_C_STOP:
            DCMSG(magenta,"parseData case NL_C_STOP: ") ;

            // received emergency stop response
            sit_client->didStop(); // tell client
            break;
        case NL_C_HIT_CAL:
            genlmsg_parse(nlh, 0, attrs, HIT_A_MAX, hit_calibration_policy);
            DCMSG(CYAN,"parseData case NL_C_HIT_CAL: attrs = 0x%x",attrs[HIT_A_MSG]) ;

            if (attrs[HIT_A_MSG]) {
                // received calibration data
                struct hit_calibration *hit_c = (struct hit_calibration*)nla_data(attrs[HIT_A_MSG]);
                struct hit_calibration lastHitCal; // get calibration data
                sit_client->getHitCal(&lastHitCal);
                if (hit_c != NULL) {
                    switch (hit_c->set) {
                        case HIT_OVERWRITE_ALL:
                        case HIT_OVERWRITE_NONE:
                            lastHitCal = *hit_c;
                            break;
                        case HIT_OVERWRITE_CAL:
                        case HIT_GET_CAL:
                            lastHitCal.seperation = hit_c->seperation;
                            DCMSG(BLUE,"setting lasthitcal.seperation to 0x%X",hit_c->seperation) ;             
                            lastHitCal.sensitivity = hit_c->sensitivity;
                            lastHitCal.blank_time = hit_c->blank_time;
                            lastHitCal.enable_on = hit_c->enable_on;
                            break;
                        case HIT_OVERWRITE_OTHER:
                            lastHitCal.type = hit_c->type;
                            lastHitCal.invert = hit_c->invert;
                            lastHitCal.hits_to_kill = hit_c->hits_to_kill;
                            lastHitCal.after_kill = hit_c->after_kill;
                            lastHitCal.bob_type = hit_c->bob_type;           
                            break;
                        case HIT_OVERWRITE_TYPE:
                        case HIT_GET_TYPE:
                            lastHitCal.type = hit_c->type;
                            lastHitCal.invert = hit_c->invert;
                            break;
                        case HIT_OVERWRITE_KILL:
                        case HIT_GET_KILL:
                            lastHitCal.hits_to_kill = hit_c->hits_to_kill;
                            lastHitCal.after_kill = hit_c->after_kill;
                            lastHitCal.bob_type = hit_c->bob_type;  
                            break;
                    }
                    sit_client->didHitCal(lastHitCal); // tell client
                }
            }

            break;
        case NL_C_HITS:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);
            data=(int *)nla_data(attrs[GEN_INT8_A_MSG]);
            DCMSG(RED,"parseData case NL_C_HITS: attrs = 0x%x, nla_data (0x%x)  =%d ",attrs[GEN_INT8_A_MSG],data,*data);

            if (attrs[GEN_INT8_A_MSG]) {
                // received hit count
                int value = nla_get_u8(attrs[GEN_INT8_A_MSG]);
                sit_client->didHits(value); // tell client
            }

            break;
        case NL_C_ACCESSORY:
            genlmsg_parse(nlh, 0, attrs, ACC_A_MAX, accessory_conf_policy);
            data=(int *)nla_data(attrs[ACC_A_MSG]);
            DCMSG(MAGENTA,"parseData case NL_C_ACCESSORY: attrs = 0x%x, nla_data (0x%x)  =%d ",attrs[ACC_A_MSG],data,*data);

            if (attrs[ACC_A_MSG]) {
                // received calibration data
                struct accessory_conf *acc_c = (struct accessory_conf*)nla_data(attrs[ACC_A_MSG]);
                switch (acc_c->acc_type) {
                    /* TODO -- fill in support for additional accessories */
                    case ACC_NES_MFS:
                        sit_client->didMFS(acc_c->exists,acc_c->on_exp, acc_c->ex_data1, acc_c->start_delay/2, acc_c->repeat_delay/2); // tell client
                        break;
                    case ACC_NES_MGL:
                        sit_client->didMGL(acc_c->exists,acc_c->on_exp); // tell client
                        break;                      
                    case ACC_NES_PHI:
                        sit_client->didPHI(acc_c->exists,acc_c->on_hit); // tell client
                        break;                      
                    case ACC_THERMAL:
                        sit_client->didTherm(acc_c->exists,acc_c->on_now); // tell client
                        break;
                    case ACC_SMOKE:
                    case ACC_SES:
                        DCMSG(RED,"Unsupported accesory message: %i\n", acc_c->acc_type)
                                break;
                    case ACC_MILES_SDH:
                        // MILES data : ex_data1 = Player ID, ex_data2 = MILES Code, ex_data3 = Ammo type, start_delay = Fire Delay
                        DCMSG(RED,"Case: ACC_MILES_SDH: exists: %i, data1: %i, data2: %i, data3: %i\n", acc_c->exists, acc_c->ex_data1, acc_c->ex_data2, acc_c->ex_data3)
                        sit_client->didMSDH(acc_c->ex_data2, acc_c->ex_data3, acc_c->ex_data1, acc_c->start_delay/2);
                        break;
                }
            }
            break;

        case NL_C_EVENT:
            genlmsg_parse(nlh, 0, attrs, GEN_INT8_A_MAX, generic_int8_policy);
            data=(int *)nla_data(attrs[GEN_INT8_A_MSG]);
            DCMSG(RED,"parseData case NL_C_EVENT: attrs = 0x%x, nla_data (0x%x)  =%d ",attrs[GEN_INT8_A_MSG],data,*data);

            break;
    }

    FUNCTION_INT("SIT_Conn::parseData(struct nl_msg *msg)", 0);
    return 0;
}

/***********************************************************
 *                   SIT Netlink Commands                   *
 ***********************************************************/
// change position to conceal
void SIT_Conn::doConceal() {
    FUNCTION_START("::doConceal()");

    // Queue command
    queueMsgU8(NL_C_EXPOSE, CONCEAL); // conceal command

    FUNCTION_END("::doConceal()");
}

// change position to expose
void SIT_Conn::doExpose() {
    FUNCTION_START("::doExpose()");

    // Queue command
    queueMsgU8(NL_C_EXPOSE, EXPOSE); // expose command

    FUNCTION_END("::doExpose()");
}

// get the current position
void SIT_Conn::doMoving() {
    FUNCTION_START("::doMoving()");

    // Queue command
    queueMsgU8(NL_C_EXPOSE, EXPOSURE_REQ); // exposure status request

    FUNCTION_END("::doMoving()");
}

// shutdown device
void SIT_Conn::doShutdown() {
    FUNCTION_START("::doShutdown()");

    // Queue command
    queueMsgU8(NL_C_BATTERY, BATTERY_SHUTDOWN); // shutdown command

    FUNCTION_END("::doShutdown()");
}

// sleep device
void SIT_Conn::doSleep() {
    FUNCTION_START("::doSleep()");

    // Queue command
    queueMsgU8(NL_C_SLEEP, SLEEP_COMMAND); // sleep command

    FUNCTION_END("::doSleep()");
}

// wake device
void SIT_Conn::doWake() {
    FUNCTION_START("::doWake()");

    // Queue command
    queueMsgU8(NL_C_SLEEP, WAKE_COMMAND); // wake command

    FUNCTION_END("::doWake()");
}

// retrieve battery value
void SIT_Conn::doBattery() {
    FUNCTION_START("::doBattery()");

    // Queue command
    queueMsgU8(NL_C_BATTERY, BATTERY_REQUEST); // battery status request

    FUNCTION_END("::doBattery()");
}

// immediate stop (stops accessories as well)
void SIT_Conn::doStop() {
    FUNCTION_START("::doStop()");

    // Queue command
    queueMsgU8(NL_C_STOP, 1); // emergency stop command

    FUNCTION_END("::doStop()");
}

// change hit calibration data
void SIT_Conn::doHitCal(struct hit_calibration hit_c) {
    FUNCTION_START("::doHitCal(struct hit_calibration hit_c)");

    // Queue command
    queueMsg(NL_C_HIT_CAL, HIT_A_MSG, sizeof(struct hit_calibration), &hit_c); // pass structure without changing

    FUNCTION_END("::doHitCal(struct hit_calibration hit_c)");
}

// change received hits to "num" (usually to 0 for reset)
void SIT_Conn::doHits(int num) {
    FUNCTION_START("SIT_Conn::doHits(int num)");

    // reset or retrieve hit count
    if (num == -1) {
        queueMsgU8(NL_C_HITS, HIT_REQ); // request hit count
    } else {
        if (num >= HIT_REQ) {
            num = HIT_REQ-1;
        }		
        queueMsgU8(NL_C_HITS, num); // reset to num
    }

    FUNCTION_END("SIT_Conn::doHits(int num)");
}

// change MSDH data
void SIT_Conn::doMSDH(int code, int ammo, int player, int delay) {
    FUNCTION_START("::doMSDH(int code, int ammo, int player, int delay)");
    // Create attribute
    struct accessory_conf acc_c;
    memset(&acc_c, 0, sizeof(struct accessory_conf)); // start zeroed out
    acc_c.acc_type = ACC_MILES_SDH;
    //acc_c.on_exp = 1; // turn on when fully exposed
    acc_c.ex_data2 = code;
    acc_c.ex_data3 = ammo;
    acc_c.ex_data1 = player;
    acc_c.start_delay = 2 * delay;
    // Turn off if delay is over 100
    if (delay <= 100) {
       acc_c.on_exp = 2;
       acc_c.on_kill = 2;  //deactivate on kill
    } else {
       acc_c.on_exp = 0;
    }

    // Queue command
    queueMsg(NL_C_ACCESSORY, ACC_A_MSG, sizeof(struct accessory_conf), &acc_c); // MSDH is an accessory

    FUNCTION_END("::doMSDH(int code, int ammo, int player, int delay)");
}

// change MFS data
void SIT_Conn::doMFS(int on, int mode, int idelay, int rdelay) {
    FUNCTION_START("::doMFS(int on, int mode, int idelay, int rdelay)");

    struct accessory_conf acc_c;

    // Create attribute
    memset(&acc_c, 0, sizeof(struct accessory_conf)); // start zeroed out\
    sit_client->getAccC(&acc_c);

    acc_c.acc_type = ACC_NES_MFS;
    if (on) {
        acc_c.on_exp = 1;    // on
        acc_c.on_kill = 2;   // 2 = deactivate on kill
    } else {
        acc_c.on_exp = 0;    // off
    }
    if (mode == 1) {
        acc_c.ex_data1 = 1; // do burst
        acc_c.ex_data2 = 5; // burst 5 times
        acc_c.on_time = 15; // on 15 milliseconds
        acc_c.off_time = 85; // off 85 milliseconds
    } else {
        acc_c.on_time = 15; // on 15 milliseconds
    }
    acc_c.repeat_delay = 2 * rdelay; // repeat every rdelay*2 half-seconds
    acc_c.repeat = 63; // infinite repeat
    acc_c.start_delay = 2 * idelay; // start after idelay*2 half-seconds

    // Queue command
    queueMsg(NL_C_ACCESSORY, ACC_A_MSG, sizeof(struct accessory_conf), &acc_c); // MFS is an accessory


    FUNCTION_END("::doMFS(int on, int mode, int idelay, int rdelay)");
}

// change MFS data
void SIT_Conn::doMGL(int on) {
    FUNCTION_START("::doMFS(int on)");

    struct accessory_conf acc_c;

    // Create attribute
    memset(&acc_c, 0, sizeof(struct accessory_conf)); // start zeroed out\
    sit_client->getAccC(&acc_c);

    acc_c.acc_type = ACC_NES_MGL;
    if (on) {
        acc_c.on_exp = 2;   // 2 = 2 for active when partially exposed and fully exposed
    } else {
        acc_c.on_exp = 0;   // off
    }

    // Queue command
    queueMsg(NL_C_ACCESSORY, ACC_A_MSG, sizeof(struct accessory_conf), &acc_c); // MFS is an accessory

    FUNCTION_END("::doMGL(int on)");
}

// change PHI data
void SIT_Conn::doPHI(int on) {
    FUNCTION_START("::doPHI(int on)");

    struct accessory_conf acc_c;

    // Create attribute
    memset(&acc_c, 0, sizeof(struct accessory_conf)); // start zeroed out\
    sit_client->getAccC(&acc_c);

    acc_c.acc_type = ACC_NES_PHI;
    if (on) {
        acc_c.on_hit = 1;   // 2 = 2 for active when partially exposed and fully exposed
        acc_c.on_time = 2000;   // time to stay on
    } else {
        acc_c.on_exp = 0;   // off
    }

    // Queue command
    queueMsg(NL_C_ACCESSORY, ACC_A_MSG, sizeof(struct accessory_conf), &acc_c); // MFS is an accessory

    FUNCTION_END("::doPHI(int on)");
}

// change Thermal data
void SIT_Conn::doTherm(int on) {
    FUNCTION_START("::doTherm(int on)");

    struct accessory_conf acc_c;

    // Create attribute
    memset(&acc_c, 0, sizeof(struct accessory_conf)); // start zeroed out\
    sit_client->getAccC(&acc_c);

    acc_c.acc_type = ACC_THERMAL;
    if (on == 1) {
        acc_c.on_now = 1;   // 1 for activate soon
    } else {
        acc_c.on_now = 0;   // off
    }
    acc_c.ex_data1 = 1;     // we are only using one thermal at a time right now
    // Queue command
    queueMsg(NL_C_ACCESSORY, ACC_A_MSG, sizeof(struct accessory_conf), &acc_c); // Thermal is an accessory

    FUNCTION_END("::doTherm(int on)");
}

// retrieve gps dataprotected:
void SIT_Conn::doGPS() {
    FUNCTION_START("::doGPS()");

    // Create attribute
    struct gps_conf gps_c;
    memset(&gps_c, 0, sizeof(struct gps_conf)); // for request, everything is zeroed out

    // Queue command
    queueMsg(NL_C_GPS, GPS_A_MSG, sizeof(struct gps_conf), &gps_c);

    FUNCTION_END("::doGPS()");
}


