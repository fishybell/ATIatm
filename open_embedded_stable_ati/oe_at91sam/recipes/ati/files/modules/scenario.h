#ifndef __SCENARIO_H__
#define __SCENARIO_H__

// run a scenario (only one globally at a time)
extern int scenario_run(char *scen); // null-terminated scenario
                                     // returns 1 if started running scenario
                                     // returns 0 if running another scenario already

// kills running scenario, if any
extern void scenario_kill(void);

//-------------------------------------------------
// Helper functions defined here so they are
//  usable in user-space programs
//-------------------------------------------------

// helper function to properly escape a function call within a function call
void escape_scen_call(char *call, int length, char *dest) {
   int i, j = 0;
   char c;
   dest[j++] = '"'; // start with a quote
   for (i=0; i<length; i++) {
      c = call[i];
      // look at characters to escape
      if (c == '{' || c == '}' || c == '"' || c == '\\') {
         dest[j++] = '\\'; // copy escape character first
      }
      // copy original character
      dest[j++] = c;
   }
   dest[j++] = '"'; // end with a quote
   dest[j++] = '\0'; // null terminate
}

// helper function to properly hex-encode attribute data (dest_buf should be 2*size+1 big)
void hex_encode_attr(const char *attr, int size, char *dest_buf) {
   int i;
   for (i=0; i<size; i++) {
      // add bytes in as hex
      snprintf(dest_buf, 3, "%02X", attr[i]);
      dest_buf+=2;
   }
   // string is null-terminated
}

// helper function to properly hex-decode attribute data (dest_buf should be length/2 big)
#if 0
// can't do this method, as apparently sscanf doesn't work the same way in the kernel
void hex_decode_attr(char *hex, int length, char *dest_buf) {
   int i, j;
   int size = length/2;
   for (i=0; i<size; i++) {
      // parse hex digit
      sscanf(hex, "%02X", &j);
      dest_buf[i] = j & 0xFF; // grab lower byte of j only
      hex+=2; // move forward two (for each hex nibble)
   }
   // does not null-terminate string
}
#endif
void hex_decode_attr(char *hex, int length, char *dest_buf) {
   int i;
   int size = length/2;
   for (i=0; i<size; i++) {
      // parse hex digit
      switch (hex[i*2]) {
         case '0' : dest_buf[i] = 0x00; break;
         case '1' : dest_buf[i] = 0x10; break;
         case '2' : dest_buf[i] = 0x20; break;
         case '3' : dest_buf[i] = 0x30; break;
         case '4' : dest_buf[i] = 0x40; break;
         case '5' : dest_buf[i] = 0x50; break;
         case '6' : dest_buf[i] = 0x60; break;
         case '7' : dest_buf[i] = 0x70; break;
         case '8' : dest_buf[i] = 0x80; break;
         case '9' : dest_buf[i] = 0x90; break;
         case 'A' : dest_buf[i] = 0xA0; break;
         case 'B' : dest_buf[i] = 0xB0; break;
         case 'C' : dest_buf[i] = 0xC0; break;
         case 'D' : dest_buf[i] = 0xD0; break;
         case 'E' : dest_buf[i] = 0xE0; break;
         case 'F' : dest_buf[i] = 0xF0; break;
      }
      switch (hex[(i*2)+1]) {
         case '0' : dest_buf[i] |= 0x00; break;
         case '1' : dest_buf[i] |= 0x01; break;
         case '2' : dest_buf[i] |= 0x02; break;
         case '3' : dest_buf[i] |= 0x03; break;
         case '4' : dest_buf[i] |= 0x04; break;
         case '5' : dest_buf[i] |= 0x05; break;
         case '6' : dest_buf[i] |= 0x06; break;
         case '7' : dest_buf[i] |= 0x07; break;
         case '8' : dest_buf[i] |= 0x08; break;
         case '9' : dest_buf[i] |= 0x09; break;
         case 'A' : dest_buf[i] |= 0x0A; break;
         case 'B' : dest_buf[i] |= 0x0B; break;
         case 'C' : dest_buf[i] |= 0x0C; break;
         case 'D' : dest_buf[i] |= 0x0D; break;
         case 'E' : dest_buf[i] |= 0x0E; break;
         case 'F' : dest_buf[i] |= 0x0F; break;
      }
   }
   // does not null-terminate string
}


/*--------------------------------------------------------------------------------------------------
 * Scenario Format:
 *
 * Calling a function:
 * {FUNCTION;ARG1;ARG2;ARG3;ARG4} // all function calls have 4 arguments
 *                                // function names are A-Z, a-z, underscore, hyphen, 0-9
 *                                // arguments can not have semi-colons or curly braces unquoted
 *                                // when passing a command or series of commands as an argument,
 *                                   surround the entire block with quotes. ex: "{;;;;} -- comment
 *                                                                               {;;;;} -- comment"
 *                                // command blocks with a need for quotes inside the block need to
 *                                   escape the inside quotes with a backslash, and potentially
 *                                   escape backslashes with backslashes as well ex:
 *                                      "{;;\"{;;;;}\";;} -- function call within a function call
 *                                       {;;\"{;;\\\"{;;;;}\\\";;\";;} -- three layers of calls"
 *                                // use of the helper function escape_scen_call() is recommended
 *
 * Calling multiple functions:
 * {;;;;} -- comment {;;;;}{;;;;} // function call between start and end curly braces
 *                                // everything between end and start curly braces is ignored
 *
 * Built-in functions:
 * {SetVar;variable;value;junk;junk} // sets a register variable (0-9) to the given string value
 *
 * {SetVarLast;variable;junk;junk;junk} // sets a register variable (0-9) to the last value received
 *                                         from an event (will be hex encoded data)
 *
 * {End;junk;junk;junk;junk}      // ends the scenario (and clears all variables, watchers, etc.)
 *
 * {Send;role;id;attribute;payload}  // sends a netlink command to the given role (1 to R_MAX-1)
 *                                   // the command id is the netlink command id
 *                                   // the attribute is the netlink attribute
 *                                   // the payload is hex encoded (no 0x at the beginning), and has
 *                                      a maximum size of 16 bytes before encoding
 *                                   // the payload can be be REG_# to use a value from a register
 *                                      variable rather than a string
 *
 * {SendWait;role;id;payload;time}   // sends a netlink command to the given role (1 to R_MAX-1)
 *                                   // the command id is the netlink command id
 *                                   // the payload is hex encoded (no 0x at the beginning), and has
 *                                      a maximum size of 16 bytes before encoding
 *                                   // the payload can be be REG_# to use a value from a register
 *                                      variable rather than a string
 *                                   // the time is the timeout value
 *                                   // the attribute is the netlink attribute
 *                                   // the attribute is assumed to be 1
 *                                   // nothing happens on timeout
 *
 * {Nothing;junk;junk;junk;junk}  // does nothing
 *
 * {Delay;time;junk;junk;junk}    // waits "time" milliseconds
 *
 * {If;variable;value;true;false} // runs command "true" if register variable (0-9) is "value"
 *                                // runs command "false" if register variable (0-9) is not "value"
 *
 * {DoWait;cmd;until;time;timeout_cmd} // runs command "cmd" and then waits either until event
 *                                        "until" is received or "time" in milliseconds has passed
 *                                     // if milliseconds "time" has passed, command "timeout_cmd"
 *                                        is ran before moving on
 *
 *
 * Notes:
 *  -- Events, for the purpose of scenarios are defined as Netlink Commands and their corresponding
 *     attributes AND Netlink Commands of type "NL_C_EVENT" (without any attribute value)
 *  -- Events are given as the string representation as defined in netlink_shared.h and
 *     target_generic_output.h
 *  -- Commands ran from ScenWait and DoWait can not call ScenWait, DoWait or Delay 
 *  -- Commands ran from If can not call If
 *  -- Commands ran from If should not change the value of the register variable compared when
 *     a sub command sleeps
 *  -- The entire scenario has a maximum length of 65536 bytes, but scenarios that long may well
 *     run out of memory well before completion, depending on the complexity of the scenario
 *  -- A more general rule-of-thumb for scenario size should be no more than 4 layers deep on
 *     function calls and no more than 200 top-level function calls
 *  -- If a scenario contains a syntax error, the scenario may either have unexpected results, or
 *     end silently
 *
 *------------------------------------------------------------------------------------------------*/

// Roles (a target, or simulator, that acts in a certain way. ie. a SIT and SAT are both lifters
enum Role {
  R_UNSPECIFIED, // no role specified
  R_LIFTER,      // lifting device
  R_MOVER,       // moving device
  R_SOUND,       // sound effects device
  R_GUNNER,      // lifting device in a TTMT that acts as the gunner
  R_DRIVER,      // lifting device in a TTMT that acts as the driver
  R_MAX,
} Role_t;

#endif // __SCENARIO_H__
