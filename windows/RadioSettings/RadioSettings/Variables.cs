using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace RadioSettings
{
    class Variables
    {
        public static string computer = "";
        public static string basestation = "";
        public static string lowmac = "";
        public static string highmac = "";
        public static string port = "";
        public static string[] advancedList = new string[17];
        public static string verbose = "0";
        public static string slottime = "130";
        public static string inittime = "50";
        public static string hunttime = "10";
        public static string slowtime = "120";
        public static string fasttime = "13";
        public static string mit_length = "11";
        public static string mat_length = "83";
        public static string mit_home = "2";
        public static string mat_home = "9";
        public static string mit_end = "9";
        public static string mat_end = "74";
        public static bool readExisting = false;

        public static void assignVariables()
        {
            Variables.computer = Variables.advancedList[0];
            Variables.basestation = Variables.advancedList[1];
            Variables.lowmac = Variables.advancedList[2];
            Variables.highmac = Variables.advancedList[3];
            Variables.verbose = Variables.advancedList[4];
            Variables.slottime = Variables.advancedList[5];
            Variables.inittime = Variables.advancedList[6];
            Variables.hunttime = Variables.advancedList[7];
            Variables.slowtime = Variables.advancedList[8];
            Variables.fasttime = Variables.advancedList[9];
            Variables.mit_length = Variables.advancedList[10];
            Variables.mat_length = Variables.advancedList[11];
            Variables.mit_home = Variables.advancedList[12];
            Variables.mat_home = Variables.advancedList[13];
            Variables.mit_end = Variables.advancedList[14];
            Variables.mat_end = Variables.advancedList[15];
            Variables.port = Variables.advancedList[16];
        }

        public static void reverseAssign1()
        {
            Variables.advancedList[0] = Variables.computer;
            Variables.advancedList[1] = Variables.basestation;
            Variables.advancedList[2] = Variables.lowmac;
            Variables.advancedList[3] = Variables.highmac;
        }
    }

}
