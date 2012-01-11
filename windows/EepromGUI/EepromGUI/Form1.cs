using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using System.Text.RegularExpressions;

namespace EepromGUI
{
    public partial class Form1 : Form
    {
        String machine = "";
        String macPassword = "change MAC";
        Eeprom conn;
        String boardType = "";
        List<Control> changedList = new List<Control>();
        //Eeprom listener;
        public delegate void serviceGUIDelegate();
        public Form1()
        {
            InitializeComponent();
        }

        /********************************************************
         * The user selected a new device so close the connection and
         * start a new one
         * ******************************************************/
        private void targetCB_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.killThread();
                conn.CloseConnection();
            }
            errorLBL.Text = "Attempting to connect...";
            errorLBL.Update();
            // Clear existing text field values
            clearFields();
            changedList.Clear();
            // Make main tcp connection
            machine = (string)targetCB.SelectedItem;
            conn = new Eeprom(
               this,
               delegate(string message, int status)
               {
                   //Populate the other parts of the GUI with updates
                   String[] group = Regex.Split(message, "\n");
                   foreach (var item in group)
                   {
                       if (item != "")
                       {
                           this.logTB.AppendText(machine + " - received: " + item + "\n");
                           logTB.ScrollToCaret();
                           parseIncoming(item + "\n");
                       }
                   }
               });
           
            if (conn.StartConnection(machine))
            {
                errorLBL.Text = "";
                showAllButton.Enabled = true;
                setBoardType();
                conn.StartProcess(machine);
            }
            else
            {
                errorLBL.Text = "Can't connect to this target at this time.";
                conn = null;
            }
            
        }

        private void batGetButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("B");
                logSent("B");
            }
        }

        private void concealButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("C");
                logSent("C");
            }
        }

        // Expose the device
        private void exposeButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("E");
                logSent("E");
            }
        }

        // Shutdown Device
        private void shutdownButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("K");
                logSent("K");
            }
        }

        // Emergency Stop
        private void stopButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("X");
                logSent("X");
            }
        }

        // Reboot the machine
        private void rebootButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("I R");
                logSent("I R");
                errorLBL.Text = "Rebooting, Don't press any buttons till finished...";
                clearFields();
                changedList.Clear();
                conn.CloseConnection();
                conn.killThread();
                timer1.Start();
            }
        }

        /***********************************************
        * Show the movement speed
        * ********************************************/
        private void moveShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("M");
                logSent("M");
            }
        }

        /***********************************************
         * Set the movement speed
         * ********************************************/
        private void moveSetButton_Click(object sender, EventArgs e)
        {
            String move = moveTB.Text;
            if (conn != null)
            {
                conn.sendMessage("M " + move);
                logSent("M " + move);
            }
        }

        /***********************************************
         * Show the hit data
         * ********************************************/
        private void hitDShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("H");
                logSent("H");
            }
        }

        /***********************************************
         * Set the hit data
         * ********************************************/
        private void hitDSetButton_Click(object sender, EventArgs e)
        {
            String hit_data = hitDTB.Text;
            if (conn != null)
            {
                conn.sendMessage("H " + hit_data);
                logSent("H " + hit_data);
            }
        }

        /***********************************************
        * Toggle conceal and expose
        * ********************************************/
        private void toggleButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("T");
                logSent("T");
            }
        }

        /***********************************************
        * Show the sleep status
        * ********************************************/
        private void sleepShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("P");
                logSent("P");
            }
        }

        /***********************************************
        * Set the sleep status
        * ********************************************/
        private void sleepSetButton_Click(object sender, EventArgs e)
        {
            int sleep = sleepCB.SelectedIndex;
            if (conn != null)
            {
                conn.sendMessage("P " + sleep);
                logSent("P " + sleep);
            }
        }

        /***********************************************
        * Show the board type
        * ********************************************/
        private void boardShowButton_Click(object sender, EventArgs e)
        {
            setBoardType();
        }

        /***********************************************
        * Set the board type to a global variable
        * ********************************************/
        private void setBoardType()
        {
            if (conn != null)
            {
                conn.sendMessage("I B");
                logSent("I B");
            }
        }

        /***********************************************
        * Set the board type
        * ********************************************/
        private void boardSetButton_Click(object sender, EventArgs e)
        {
            String board = (String)boardCB.SelectedItem;
            if (conn != null)
            {
                conn.sendMessage("I B " + board);
                logSent("I B " + board);
            }
            disableEnable(boardType);
        }

        /***********************************************
        * Show the communication type
        * ********************************************/
        private void commShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("I D");
                logSent("I D");
            }
        }

        /***********************************************
        * Set the communication type
        * ********************************************/
        private void commSetButton_Click(object sender, EventArgs e)
        {
            String comm = (String)commCB.SelectedItem;
            if (conn != null)
            {
                conn.sendMessage("I D " + comm);
                logSent("I D " + comm);
            }
        }

        /***********************************************
        * Show the MAC address
        * ********************************************/
        private void macShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("I M");
                logSent("I M");
            }
        }

        /***********************************************
        * Show the password popup when the mac is
         * getting changed
        * ********************************************/
        private void macSetButton_Click(object sender, EventArgs e)
        {
            // Make a password popup box
            passwordPanel.Visible = true;
            passLabel1.Text = "Call Action Target customer service";
            passLabel2.Text = "for the password.";
        }

        /***********************************************
        * Set the MAC address
        * ********************************************/
        private void okButton_Click(object sender, EventArgs e)
        {
            if (passwordTB.Text == macPassword)
            {
                String mac = macTB.Text;
                if (conn != null)
                {
                    conn.sendMessage("I M " + mac);
                    logSent("I M " + mac);
                }
            }
            else
            {
                passLabel1.Text = "";
                passLabel2.Text = "Incorrect Password";
            }
        }

        /*********************************************
         * Hides the password panel
         * ******************************************/
        private void cancelButton_Click(object sender, EventArgs e)
        {
            passwordPanel.Visible = false;
        }



        /*********************************************
         * Show the listen port number
         * ******************************************/
        private void listenShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("I L");
                logSent("I L");
            }
        }

        /*********************************************
         * Set the listen port number
         * ******************************************/
        private void listenSetButton_Click(object sender, EventArgs e)
        {
            String listen = listenTB.Text;
            if (conn != null)
            {
                conn.sendMessage("I L " + listen);
                logSent("I L " + listen);
            }
        }

        /*********************************************
         * Show the connect port number
         * ******************************************/
        private void connectShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("I C");
                logSent("I C");
            }
        }

        /*********************************************
         * Set the connect port number
         * ******************************************/
        private void connectSetButton_Click(object sender, EventArgs e)
        {
            String connect = connectTB.Text;
            if (conn != null)
            {
                conn.sendMessage("I C " + connect);
                logSent("I C " + connect);
            }
        }

        /*****************************************
         * Show the IP address
         * **************************************/
        private void ipShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("I I");
                logSent("I I");
            }
        }

        /*****************************************
         * Set the IP address
         * **************************************/
        private void ipSetButton_Click(object sender, EventArgs e)
        {
            String ip = ipTB.Text;
            if (conn != null)
            {
                conn.sendMessage("I I " + ip);
                logSent("I I " + ip);
            }
        }

        /********************************************************
         * Show the fall parameters
         * *****************************************************/
        private void fallShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("F");
                logSent("F");
            }
        }

        /********************************************************
        * Set the fall parameters
        * *****************************************************/
        private void fallSetButton_Click(object sender, EventArgs e)
        {
            String fKill = fkillTB.Text;
            int fParams = fallCB.SelectedIndex;
            if (conn != null)
            {
                conn.sendMessage("F " + fKill + " " + fParams);
                logSent("F " + fKill + " " + fParams);
            }
        }

        /****************************************************
         * Send event to the kernal
         * *************************************************/
        private void eventButton_Click(object sender, EventArgs e)
        {
            int sendEvent = eventCB.SelectedIndex;
            if (conn != null)
            {
                conn.sendMessage("V " + sendEvent);
                logSent("V " + sendEvent);
            }
        }

        /*************************************************
         * The drop down of the event combo box is exposed
         * ***********************************************/
        private void dropDown_shown(object sender, EventArgs e)
        {
            eventCB.ForeColor = System.Drawing.SystemColors.WindowText;
        }

        /*************************************************
         * Show the hit sensor settings
         * **********************************************/
        private void sensorShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("Y");
                logSent("Y");
            }
        }

        /*************************************************
        * Set the hit sensor settings
        * **********************************************/
        private void sensorSetButton_Click(object sender, EventArgs e)
        {
            int sensor1 = sensorCB.SelectedIndex;
            int sensor2 = sensor2CB.SelectedIndex;
            if (conn != null)
            {
                conn.sendMessage("Y " + sensor1 + " " + sensor2);
                logSent("Y " + sensor1 + " " + sensor2);
            }
        }

        /*************************************************
        * Show the hit calibration
        * **********************************************/
        private void calShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("L");
                logSent("L");
            }
        }

        /*************************************************
        * Set the hit calibration
        * **********************************************/
        private void calSetButton_Click(object sender, EventArgs e)
        {
            int cal1 = Convert.ToInt32(calTB1.Text);
            int cal2 = Convert.ToInt32(calTB2.Text);
            int cal3 = Convert.ToInt32(calTB3.Text);
            int cal4 = calCB4.SelectedIndex;
            if (conn != null)
            {
                conn.sendMessage("L " + cal1 + " " + cal2 + " " + cal3 + " " + cal4);
                logSent("L " + cal1 + " " + cal2 + " " + cal3 + " " + cal4);
            }
        }

        /******************************************************
        * Show the accessory details
        * ***************************************************/
        private void accShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                String acc = Convert.ToString(accCB0.SelectedItem);
                conn.sendMessage("Q " + acc);
                logSent("Q " + acc);
            }
        }

        /******************************************************
        * Set the accessory details
        * ***************************************************/
        private void accSetButton_Click(object sender, EventArgs e)
        {
            String acc1 = (string)accCB0.SelectedItem;
            int acc3 = (int)accCB2.SelectedIndex;
            int acc4 = (int)accCB3.SelectedIndex;
            int acc5 = (int)accCB4.SelectedIndex;
            int acc6 = (int)accCB5.SelectedIndex;
            String acc7 = accTB1.Text;
            String acc8 = accTB2.Text;
            String acc9 = accTB3.Text;
            String acc10 = accTB4.Text;
            String acc11 = accTB5.Text;
            String message = "Q " + acc1 + " " + acc3 + " " + acc4 + " " + acc5 + " " + acc6
                + " " + acc7 + " " + acc8 + " " + acc9 + " " + acc10 + " " + acc11;
            if (conn != null)
            {
                conn.sendMessage(message);
                logSent(message);
            }
        }

        /**********************************************************
         * Show the position
         * *******************************************************/
        private void posShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("A");
                logSent("A");
            }
        }

        /*************************************************
         * Show the exposure status
         * **********************************************/
        private void expSShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("S");
                logSent("S");
            }
        }

        /***********************************************
         * Show the GPS location
         * ********************************************/
        private void gpsShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("G");
                logSent("G");
            }
        }

        /***********************************************
        * Show the knob information
        * ********************************************/
        private void knobShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("Z");
                logSent("Z");
            }
        }

        /************************************************
         * Show the SES mode
         * *********************************************/
        private void modeShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("O");
                logSent("O");
            }
        }

        /************************************************
        * Set the SES mode
        * *********************************************/
        private void modeSetButton_Click(object sender, EventArgs e)
        {
            String mode = modeTB.Text;
            if (conn != null)
            {
                conn.sendMessage("O " + mode);
                logSent("O " + mode);
            }
        }

        // Show full hit data
        /*private void fHitShowButton_Click(object sender, EventArgs e)
        {
            //String fHit = Server.ConnectEeprom("D", machine);
            String fHit = conn.sendMessage("D");
            if (fHit.StartsWith("line"))
            {
                fHit = fHit.Replace("n\n", "n,");
                fHit = fHit.Replace("\n", "");
                String[] numbs = fHit.Split(',');

                fHitTB1.Text = numbs[3];
                fHitTB2.Text = numbs[4];
                fHitTB3.Text = numbs[5];
            }
        }*/

        /**********************************************
         * Show all settings
         * ********************************************/
        private void showAllButton_Click(object sender, EventArgs e)
        {
            // Grab only the information available for the current board type
            targetCB.SelectedItem = machine;
            switch (boardType)
            {
                case "SES":
                    batGetButton_Click(sender, e);
                    sleepShowButton_Click(sender, e);
                    boardShowButton_Click(sender, e);
                    commShowButton_Click(sender, e);
                    macShowButton_Click(sender, e);
                    listenShowButton_Click(sender, e);
                    connectShowButton_Click(sender, e);
                    ipShowButton_Click(sender, e);
                    knobShowButton_Click(sender, e);
                    modeShowButton_Click(sender, e);
                    break;
                case "SIT":
                    batGetButton_Click(sender, e);
                    hitDShowButton_Click(sender, e);
                    sleepShowButton_Click(sender, e);
                    boardShowButton_Click(sender, e);
                    commShowButton_Click(sender, e);
                    macShowButton_Click(sender, e);
                    listenShowButton_Click(sender, e);
                    connectShowButton_Click(sender, e);
                    ipShowButton_Click(sender, e);
                    fallShowButton_Click(sender, e);
                    sensorShowButton_Click(sender, e);
                    calShowButton_Click(sender, e);
                    accShowButton_Click(sender, e);
                    expSShowButton_Click(sender, e);
                    break;
                case "SAT":
                    batGetButton_Click(sender, e);
                    hitDShowButton_Click(sender, e);
                    sleepShowButton_Click(sender, e);
                    boardShowButton_Click(sender, e);
                    commShowButton_Click(sender, e);
                    macShowButton_Click(sender, e);
                    listenShowButton_Click(sender, e);
                    connectShowButton_Click(sender, e);
                    ipShowButton_Click(sender, e);
                    fallShowButton_Click(sender, e);
                    sensorShowButton_Click(sender, e);
                    calShowButton_Click(sender, e);
                    accShowButton_Click(sender, e);
                    //fHitShowButton_Click(sender, e);
                    expSShowButton_Click(sender, e);
                    break;
                case "MIT":
                    batGetButton_Click(sender, e);
                    moveShowButton_Click(sender, e);
                    sleepShowButton_Click(sender, e);
                    boardShowButton_Click(sender, e);
                    commShowButton_Click(sender, e);
                    macShowButton_Click(sender, e);
                    listenShowButton_Click(sender, e);
                    connectShowButton_Click(sender, e);
                    ipShowButton_Click(sender, e);
                    posShowButton_Click(sender, e);
                    break;
                case "MAT":
                    batGetButton_Click(sender, e);
                    moveShowButton_Click(sender, e);
                    sleepShowButton_Click(sender, e);
                    boardShowButton_Click(sender, e);
                    commShowButton_Click(sender, e);
                    macShowButton_Click(sender, e);
                    listenShowButton_Click(sender, e);
                    connectShowButton_Click(sender, e);
                    ipShowButton_Click(sender, e);
                    posShowButton_Click(sender, e);
                    break;
                default:
                    Console.WriteLine("Not an approved device type");
                    break;
            }
        }

        /**************************************
         * Disable buttons not related to the 
         * board type
         * ***********************************/
        private void disableEnable(String type)
        {
            switch (type) {
                case "SES":
                    enableAll();
                    moveShowButton.Enabled = false;
                    moveSetButton.Enabled = false;
                    posShowButton.Enabled = false;
                    concealButton.Enabled = false;
                    exposeButton.Enabled = false;
                    toggleButton.Enabled = false;
                    fallSetButton.Enabled = false;
                    fallShowButton.Enabled = false;
                    hitDSetButton.Enabled = false;
                    hitDShowButton.Enabled = false;
                    sensorSetButton.Enabled = false;
                    sensorShowButton.Enabled = false;
                    calSetButton.Enabled = false;
                    calShowButton.Enabled = false;
                    accSetButton.Enabled = false;
                    accShowButton.Enabled = false;
                    //fHitShowButton.Enabled = false;
                    expSShowButton.Enabled = false;
                    break;
                case "SIT":
                    enableAll();
                    moveShowButton.Enabled = false;
                    moveSetButton.Enabled = false;
                    posShowButton.Enabled = false;
                    knobShowButton.Enabled = false;
                    modeShowButton.Enabled = false;
                    modeSetButton.Enabled = false;
                    break;
                case "SAT":
                    enableAll();
                    moveShowButton.Enabled = false;
                    moveSetButton.Enabled = false;
                    posShowButton.Enabled = false;
                    knobShowButton.Enabled = false;
                    modeShowButton.Enabled = false;
                    modeSetButton.Enabled = false;
                    break;
                case "MIT":
                    enableAll();
                    modeShowButton.Enabled = false;
                    modeSetButton.Enabled = false;
                    knobShowButton.Enabled = false;
                    concealButton.Enabled = false;
                    exposeButton.Enabled = false;
                    toggleButton.Enabled = false;
                    fallSetButton.Enabled = false;
                    fallShowButton.Enabled = false;
                    hitDSetButton.Enabled = false;
                    hitDShowButton.Enabled = false;
                    sensorSetButton.Enabled = false;
                    sensorShowButton.Enabled = false;
                    calSetButton.Enabled = false;
                    calShowButton.Enabled = false;
                    accSetButton.Enabled = false;
                    accShowButton.Enabled = false;
                    //fHitShowButton.Enabled = false;
                    expSShowButton.Enabled = false;
                    break;
                case "MAT":
                    enableAll();
                    modeShowButton.Enabled = false;
                    modeSetButton.Enabled = false;
                    knobShowButton.Enabled = false;
                    concealButton.Enabled = false;
                    exposeButton.Enabled = false;
                    toggleButton.Enabled = false;
                    fallSetButton.Enabled = false;
                    fallShowButton.Enabled = false;
                    hitDSetButton.Enabled = false;
                    hitDShowButton.Enabled = false;
                    sensorSetButton.Enabled = false;
                    sensorShowButton.Enabled = false;
                    calSetButton.Enabled = false;
                    calShowButton.Enabled = false;
                    accSetButton.Enabled = false;
                    accShowButton.Enabled = false;
                    //fHitShowButton.Enabled = false;
                    expSShowButton.Enabled = false;
                    break;
            }
        }

        /****************************************
         * Enables all possibly disabled controls
         * *************************************/
        private void enableAll()
        {
            moveShowButton.Enabled = true;
            moveSetButton.Enabled = true;
            posShowButton.Enabled = true;
            modeShowButton.Enabled = true;
            modeSetButton.Enabled = true;
            knobShowButton.Enabled = true;
            concealButton.Enabled = true;
            exposeButton.Enabled = true;
            toggleButton.Enabled = true;
            fallSetButton.Enabled = true;
            fallShowButton.Enabled = true;
            hitDSetButton.Enabled = true;
            hitDShowButton.Enabled = true;
            sensorSetButton.Enabled = true;
            sensorShowButton.Enabled = true;
            calSetButton.Enabled = true;
            calShowButton.Enabled = true;
            accSetButton.Enabled = true;
            accShowButton.Enabled = true;
            expSShowButton.Enabled = true;
        }

        /**************************************************
         * Clears all the textbox fields
         * ************************************************/
        private void clearFields()
        {
            moveTB.Text = "";
            sleepCB.SelectedIndex = -1;
            hitDTB.Text = "";
            eventCB.SelectedIndex = -1;
            batTB.Text = "";
            posTB.Text = "";
            expSTB.Text = "";
            gpsTB.Text = "";
            knobTB.Text = "";
            knobTB2.Text = "";
            boardCB.SelectedIndex = -1;
            commCB.SelectedIndex = -1;
            macTB.Text = "";
            listenTB.Text = "";
            connectTB.Text = "";
            ipTB.Text = "";
            modeTB.Text = "";
            fkillTB.Text = "";
            fallCB.SelectedIndex = -1;
            sensorCB.SelectedIndex = -1;
            sensor2CB.SelectedIndex = -1;
            calTB1.Text = "";
            calTB2.Text = "";
            calTB3.Text = "";
            calCB4.SelectedIndex = -1;
            accCB0.Text = "";
            accCB2.Text = "";
            accCB3.Text = "";
            accCB4.Text = "";
            accCB5.Text = "";
            accTB0.Text = "";
            accTB1.Text = "";
            accTB2.Text = "";
            accTB3.Text = "";
            accTB4.Text = "";
            accTB5.Text = "";
        }

        /****************************************************
         * Close the connections when the form is closed
         * *************************************************/
        private void form_closed(object sender, FormClosedEventArgs e)
        {
            if (conn != null)
            {
                conn.killThread();
                conn.CloseConnection();
            }
        }

        /**************************************************
         * Parse incoming messages to find out what part of
         * the GUI to update
         * ************************************************/
        private void parseIncoming(String message)
        {
            char first = message.ElementAt(0);
            char second = '!';
            if (message.Length > 2)
            {
                second = message.ElementAt(2);
            }
            switch (first)
            {
                case 'A':   // position
                    posTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    posTB.Text = getMessageValue(message, 2);
                    break;
                case 'B':  
                    switch (second)
                    {
                        case 'T':   
                            String knob = getMessageValue(message, 0);
                            String[] knobSplit = knob.Split(' ');
                            if (knobSplit[1] == "KNOB")         // knob information
                            {
                                knobTB.Text = knobSplit[0] + " " + knobSplit[1];
                                if (Convert.ToInt32(knobSplit[2]) == 0)
                                {
                                    knobTB2.Text = "off";
                                }
                                else
                                {
                                    knobTB2.Text = "on";
                                }
                            }
                            else if (knobSplit[1] == "MODE")    // mode information
                            {
                                String mode = getMessageValue(message, 0); 
                                String[] modeSplit = mode.Split(' ');
                                modeTB.Text = modeSplit[2];
                            }
                            break;
                        default:   // battery
                            batTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                            batTB.Text = getMessageValue(message, 2);
                            break;
                    }
                    break;
                case 'C':   // target is concealed
                    expSTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    expSTB.Text = "concealed";
                    break;
                case 'E':   // target is exposed
                    expSTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    expSTB.Text = "exposed";
                    break;
                case 'F':   // fall parameters
                    String fall = getMessageValue(message, 2);
                    String[] numbs = fall.Split(' ');
                    fkillTB.Text = numbs[0];
                    fallCB.SelectedIndex = Convert.ToInt32(numbs[1]);
                    break;
                case 'H':   // hit data
                    hitDTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    hitDTB.Text = getMessageValue(message, 2);
                    break;
                case 'L':   // hit calibration
                    String cal = getMessageValue(message, 2);
                    String[] calSplit = cal.Split(' ');
                    calTB1.Text = calSplit[0];
                    calTB2.Text = calSplit[1];
                    calTB3.Text = calSplit[2];
                    calCB4.SelectedIndex = Convert.ToInt32(calSplit[3]);
                    break;
                case 'M':   // move speed
                    moveTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    moveTB.Text = getMessageValue(message, 2);
                    break;
                case 'P':   // sleep status
                    sleepCB.SelectedIndex = Convert.ToInt32(getMessageValue(message, 2));
                    break;
                case 'Q':   // accessory
                    String acc = getMessageValue(message, 2);
                    String[] accSplit = acc.Split(' ');
                    accCB0.SelectedItem = accSplit[0];
                    // Exists = 1
                    if (Convert.ToInt32(accSplit[1]) == 1)
                    {
                        accTB0.Text = "Exists";
                    }
                    else
                    {
                        accTB0.Text = "Doesn't Exist";
                    }
                    accCB2.SelectedIndex = Convert.ToInt32(accSplit[2]);
                    accCB3.SelectedIndex = Convert.ToInt32(accSplit[3]);
                    accCB4.SelectedIndex = Convert.ToInt32(accSplit[4]);
                    accCB5.SelectedIndex = Convert.ToInt32(accSplit[5]);
                    accTB1.Text = accSplit[6];
                    accTB2.Text = accSplit[7];
                    accTB3.Text = accSplit[8];
                    accTB4.Text = accSplit[9];
                    accTB5.Text = accSplit[10];
                    break;
                case 'S':   // concealed or exposed status
                    expSTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    String status = getMessageValue(message, 2);
                    if (Convert.ToInt32(status) == 0)
                    {
                        expSTB.Text = "concealed";
                    }
                    else if (Convert.ToInt32(status) == 1)
                    {
                        expSTB.Text = "exposed";
                    }
                    else
                    {
                        expSTB.Text = "moving";
                    }
                    break;
                case 'V':   // current event being called
                    eventCB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    eventCB.SelectedIndex = Convert.ToInt32(getMessageValue(message, 2));
                    break;
                case 'Y':   // hit sensor
                    String sensor = getMessageValue(message, 2);
                    String[] sensorSplit = sensor.Split(' ');
                    sensorCB.SelectedIndex = Convert.ToInt32(sensorSplit[0]);
                    sensor2CB.SelectedIndex = Convert.ToInt32(sensorSplit[1]);
                    break;
                case 'I':   // eeprom board settings
                    switch (second)
                    {
                        case 'A':   // Address
                            addressDTB.Text = getMessageValue(message, 4);
                            addressDTB.ForeColor = System.Drawing.SystemColors.WindowText;
                            break;
                        case 'B':   // target type (SIT, MIT, etc.)
                            String board = getMessageValue(message, 4);
                            boardType = board;
                            deviceTB.Text = board;
                            boardCB.SelectedItem = board;
                            // Enable and disable controls according to the board type
                            disableEnable(boardType);
                            break;
                        case 'C':   // connection port
                            connectTB.Text = getMessageValue(message, 4);
                            break;
                        case 'D':   // communication type
                            commCB.SelectedItem = getMessageValue(message, 4);
                            break;
                        case 'E':   // battery/moving defaults
                            String batMov = getMessageValue(message, 4);
                            String[] batMovList = batMov.Split(' ');
                                switch (batMovList[0])
	                            {
                                    case "SIT":
                                        sitBTB.Text = batMovList[1];
                                        sitBTB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        break;
                                    case "SAT":
                                        satBTB.Text = batMovList[1];
                                        satBTB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        break;
                                    case "SES":
                                        sesBTB.Text = batMovList[1];
                                        sesBTB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        break;
                                    case "MIT":
                                        if (batMovList[1] == "TYPE")
                                        {
                                            mitTTB.Text = batMovList[2];
                                            mitTTB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        }
                                        else if (batMovList[1] == "REVERSE")
                                        {
                                            mitRTB.Text = batMovList[2];
                                            mitRTB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        }
                                        else
                                        {
                                            mitBTB.Text = batMovList[1];
                                            mitBTB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        }
                                        break;
                                    case "MAT":
                                       if (batMovList[1] == "TYPE")
                                        {
                                            matTTB.Text = batMovList[2];
                                            matTTB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        }
                                        else if (batMovList[1] == "REVERSE")
                                        {
                                            matRTB.Text = batMovList[2];
                                            matRTB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        }
                                        else
                                        {
                                            matBTB.Text = batMovList[1];
                                            matBTB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        }
                                        break;
                                    default:
                                        break;
	                            }
                            break;
                        case 'F':   // Fall parameter defaults
                            String[] fallSplit = getMessageValue(message, 4).Split(' ');
                            fallDTB.Text = fallSplit[0];
                            fallDCB.SelectedIndex = Convert.ToInt32(fallSplit[1]);
                            fallDTB.ForeColor = System.Drawing.SystemColors.WindowText;
                            fallDCB.ForeColor = System.Drawing.SystemColors.WindowText;
                            break;
                        case 'G':   // MGL defaults
                            String[] mglSplit = getMessageValue(message, 4).Split(' ');
                            mglCheck.Checked = Convert.ToBoolean(Convert.ToInt32(mglSplit[0]));
                            mglCB1.SelectedIndex = Convert.ToInt32(mglSplit[1]);
                            mglCB2.SelectedIndex = Convert.ToInt32(mglSplit[2]);
                            mglCB3.SelectedIndex = Convert.ToInt32(mglSplit[3]);
                            mglCB4.SelectedIndex = Convert.ToInt32(mglSplit[4]);
                            mglTB1.Text = mglSplit[5];
                            mglTB2.Text = mglSplit[6];
                            mglTB3.Text = mglSplit[7];
                            mglTB4.Text = mglSplit[8];
                            mglTB5.Text = mglSplit[9];
                            mglCheck.ForeColor = System.Drawing.SystemColors.WindowText;
                            mglCB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            mglCB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            mglCB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            mglCB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            mglTB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            mglTB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            mglTB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            mglTB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            mglTB5.ForeColor = System.Drawing.SystemColors.WindowText;
                            break;
                        case 'H':   // Hit calibration defaults
                            String[] calDSplit = getMessageValue(message, 4).Split(' ');
                            hitcDTB1.Text = calDSplit[0];
                            hitcDTB2.Text = calDSplit[1];
                            hitcDTB3.Text = calDSplit[2];
                            hitcCB4.SelectedIndex = Convert.ToInt32(calDSplit[3]);
                            hitcDTB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            hitcDTB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            hitcDTB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            hitcCB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            break;
                        case 'I':   // IP address
                            ipTB.Text = getMessageValue(message, 4);
                            break;
                        case 'K':   // SMK defaults
                            String[] smkSplit = getMessageValue(message, 4).Split(' ');
                            smkCheck.Checked = Convert.ToBoolean(Convert.ToInt32(smkSplit[0]));
                            smkCB1.SelectedIndex = Convert.ToInt32(smkSplit[1]);
                            smkCB2.SelectedIndex = Convert.ToInt32(smkSplit[2]);
                            smkCB3.SelectedIndex = Convert.ToInt32(smkSplit[3]);
                            smkCB4.SelectedIndex = Convert.ToInt32(smkSplit[4]);
                            smkTB1.Text = smkSplit[5];
                            smkTB2.Text = smkSplit[6];
                            smkTB3.Text = smkSplit[7];
                            smkTB4.Text = smkSplit[8];
                            smkTB5.Text = smkSplit[9];
                            smkCheck.ForeColor = System.Drawing.SystemColors.WindowText;
                            smkCB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            smkCB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            smkCB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            smkCB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            smkTB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            smkTB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            smkTB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            smkTB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            smkTB5.ForeColor = System.Drawing.SystemColors.WindowText;
                            break;
                        case 'L':   // listen port
                            listenTB.Text = getMessageValue(message, 4);
                            break;
                        case 'M':   // MAC address
                            String valid = getMessageValue(message, 4);
                            if (valid == "Invalid MAC address")
                            {
                                passLabel1.Text = "";
                                passLabel2.Text = "Invalid MAC address";
                            }
                            else
                            {
                                macTB.Text = valid;
                                passwordPanel.Visible = false;
                            }
                            break;
                        case 'N':   // MFS defaults
                            String[] mfsSplit = getMessageValue(message, 4).Split(' ');
                            mfsCheck.Checked = Convert.ToBoolean(Convert.ToInt32(mfsSplit[0]));
                            mfsCB1.SelectedIndex = Convert.ToInt32(mfsSplit[1]);
                            mfsCB2.SelectedIndex = Convert.ToInt32(mfsSplit[2]);
                            mfsCB3.SelectedIndex = Convert.ToInt32(mfsSplit[3]);
                            mfsCB4.SelectedIndex = Convert.ToInt32(mfsSplit[4]);
                            mfsTB1.Text = mfsSplit[5];
                            mfsTB2.Text = mfsSplit[6];
                            mfsTB3.Text = mfsSplit[7];
                            mfsTB4.Text = mfsSplit[8];
                            mfsTB5.Text = mfsSplit[9];
                            mfsCheck.ForeColor = System.Drawing.SystemColors.WindowText;
                            mfsCB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            mfsCB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            mfsCB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            mfsCB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            mfsTB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            mfsTB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            mfsTB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            mfsTB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            mfsTB5.ForeColor = System.Drawing.SystemColors.WindowText;
                            break;
                        case 'P':   // PHI defaults
                            String[] phiSplit = getMessageValue(message, 4).Split(' ');
                            phiCheck.Checked = Convert.ToBoolean(Convert.ToInt32(phiSplit[0]));
                            phiCB1.SelectedIndex = Convert.ToInt32(phiSplit[1]);
                            phiCB2.SelectedIndex = Convert.ToInt32(phiSplit[2]);
                            phiCB3.SelectedIndex = Convert.ToInt32(phiSplit[3]);
                            phiCB4.SelectedIndex = Convert.ToInt32(phiSplit[4]);
                            phiTB1.Text = phiSplit[5];
                            phiTB2.Text = phiSplit[6];
                            phiTB3.Text = phiSplit[7];
                            phiTB4.Text = phiSplit[8];
                            phiTB5.Text = phiSplit[9];
                            phiCheck.ForeColor = System.Drawing.SystemColors.WindowText;
                            phiCB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            phiCB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            phiCB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            phiCB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            phiTB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            phiTB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            phiTB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            phiTB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            phiTB5.ForeColor = System.Drawing.SystemColors.WindowText;
                            break;
                        case 'S':   // Hit sensor defaults
                            String[] hitSplit = getMessageValue(message, 4).Split(' ');
                            sensorDCB.SelectedIndex = Convert.ToInt32(hitSplit[0]);
                            sensorD2CB.SelectedIndex = Convert.ToInt32(hitSplit[1]);
                            sensorDCB.ForeColor = System.Drawing.SystemColors.WindowText;
                            sensorD2CB.ForeColor = System.Drawing.SystemColors.WindowText;
                            break;
                        case 'T':   // THM defaults
                            String[] thmSplit = getMessageValue(message, 4).Split(' ');
                            thmCheck.Checked = Convert.ToBoolean(Convert.ToInt32(thmSplit[0]));
                            thmCB1.SelectedIndex = Convert.ToInt32(thmSplit[1]);
                            thmCB2.SelectedIndex = Convert.ToInt32(thmSplit[2]);
                            thmCB3.SelectedIndex = Convert.ToInt32(thmSplit[3]);
                            thmCB4.SelectedIndex = Convert.ToInt32(thmSplit[4]);
                            thmTB1.Text = thmSplit[5];
                            thmTB2.Text = thmSplit[6];
                            thmTB3.Text = thmSplit[7];
                            thmTB4.Text = thmSplit[8];
                            thmTB5.Text = thmSplit[9];
                            thmCheck.ForeColor = System.Drawing.SystemColors.WindowText;
                            thmCB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            thmCB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            thmCB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            thmCB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            thmTB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            thmTB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            thmTB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            thmTB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            thmTB5.ForeColor = System.Drawing.SystemColors.WindowText;
                            break;
                        case 'X':   // Serial Number
                            serialDTB.Text = getMessageValue(message, 4);
                            serialDTB.ForeColor = System.Drawing.SystemColors.WindowText;
                            break;
                    }
                    break;
                default:
                    break;
            }
            changedList.Clear();
        }

        /*******************************************************
         * Returns only the value part of the incoming messages.
         * *****************************************************/
        private String getMessageValue(String message, int charIn)
        {
            int newline = message.IndexOf("\n");
            String value = message.Substring(charIn, newline - charIn);
            return value;
        }

        /************************************
         * Send a message to the log box.
         * *********************************/
        private void logSent(String sent)
        {
            logTB.AppendText(machine + " - sent: " + sent + "\n");
            logTB.ScrollToCaret();
        }

        /***************************************************
         * Check to see when a reboot is finished by checking
         * the connection.
         * ************************************************/
        private void timer1_Tick(object sender, EventArgs e)
        {
            if (conn.StartConnection(machine))
            {
                errorLBL.Text = "";
                showAllButton.Enabled = true;
                setBoardType();
                conn.StartProcess(machine);
                Console.WriteLine("Connected");
                timer1.Stop();
            }
        }

        /**************************************************
         * Events for all text changes in texboxes or
         * index changes for combo boxes in the default tab
         * ***********************************************/
        private void sitBTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(sitBTB);
            sitBTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void satBTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(satBTB);
            satBTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void sesBTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(sesBTB);
            sesBTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mitBTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mitBTB);
            mitBTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void matBTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(matBTB);
            matBTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mitTTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mitTTB);
            mitTTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mitRTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mitRTB);
            mitRTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void matTTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(matTTB);
            matTTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void matRTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(matRTB);
            matRTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void serialDTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(serialDTB);
            serialDTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void addressDTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(addressDTB);
            addressDTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void fallDTB_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(fallDTB);
            fallDTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void fallDCB_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(fallDCB);
            fallDCB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void sensorDCB_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(sensorDCB);
            sensorDCB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void sensorD2CB_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(sensorD2CB);
            sensorD2CB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void hitcDTB1_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(hitcDTB1);
            hitcDTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void hitcDTB2_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(hitcDTB2);
            hitcDTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void hitcDTB3_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(hitcDTB3);
            hitcDTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void hitcCB4_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(hitcCB4);
            hitcCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsCheck_CheckedChanged(object sender, EventArgs e)
        {
            changedList.Add(mfsCheck);
            mfsCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsCB1_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(mfsCB1);
            mfsCB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsCB2_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(mfsCB2);
            mfsCB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsCB3_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(mfsCB3);
            mfsCB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsCB4_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(mfsCB4);
            mfsCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsTB1_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mfsTB1);
            mfsTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsTB2_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mfsTB2);
            mfsTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsTB3_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mfsTB3);
            mfsTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsTB4_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mfsTB4);
            mfsTB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsTB5_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mfsTB5);
            mfsTB5.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglCheck_CheckedChanged(object sender, EventArgs e)
        {
            changedList.Add(mglCheck);
            mglCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglCB1_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(mglCB1);
            mglCB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglCB2_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(mglCB2);
            mglCB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglCB3_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(mglCB3);
            mglCB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglCB4_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(mglCB4);
            mglCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglTB1_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mglTB1);
            mglTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglTB2_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mglTB2);
            mglTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglTB3_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mglTB3);
            mglTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglTB4_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mglTB4);
            mglTB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglTB5_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(mglTB5);
            mglTB5.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiCheck_CheckedChanged(object sender, EventArgs e)
        {
            changedList.Add(phiCheck);
            phiCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiCB1_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(phiCB1);
            phiCB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiCB2_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(phiCB2);
            phiCB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiCB3_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(phiCB3);
            phiCB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiCB4_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(phiCB4);
            phiCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiTB1_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(phiTB1);
            phiTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiTB2_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(phiTB2);
            phiTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiTB3_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(phiTB3);
            phiTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiTB4_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(phiTB4);
            phiTB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiTB5_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(phiTB5);
            phiTB5.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkCheck_CheckedChanged(object sender, EventArgs e)
        {
            changedList.Add(smkCheck);
            smkCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkCB1_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(smkCB1);
            smkCB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkCB2_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(smkCB2);
            smkCB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkCB3_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(smkCB3);
            smkCB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkCB4_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(smkCB4);
            smkCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkTB1_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(smkTB1);
            smkTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkTB2_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(smkTB2);
            smkTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkTB3_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(smkTB3);
            smkTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkTB4_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(smkTB4);
            smkTB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkTB5_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(smkTB5);
            smkTB5.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmCheck_CheckedChanged(object sender, EventArgs e)
        {
            changedList.Add(thmCheck);
            thmCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmCB1_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(thmCB1);
            thmCB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmCB2_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(thmCB2);
            thmCB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmCB3_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(thmCB3);
            thmCB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmCB4_SelectedIndexChanged(object sender, EventArgs e)
        {
            changedList.Add(thmCB4);
            thmCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmTB1_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(thmTB1);
            thmTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmTB2_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(thmTB2);
            thmTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmTB3_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(thmTB3);
            thmTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmTB4_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(thmTB4);
            thmTB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmTB5_TextChanged(object sender, EventArgs e)
        {
            changedList.Add(thmTB5);
            thmTB5.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        /**************************************************
         * Parses all the default controls to see which
         * textboxes, comboboxes, or checkboxes were altered
         * in order to set the new values.
         * ***********************************************/
        private void batDSetButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                //Edit each control in the list only once
                foreach (Control c in changedList.Distinct().ToList())
                {
                    String type = c.Name;
                    String textValue = c.Text;
                    c.ForeColor = System.Drawing.SystemColors.WindowText;
                    switch (type)
                    {
                        case "sitBTB":
                            batDefaults(1, textValue);
                            break;
                        case "satBTB": 
                            batDefaults(2, textValue);
                            break;
                        case "sesBTB":
                            batDefaults(3, textValue);
                            break;
                        case "mitBTB":
                            batDefaults(4, textValue);
                            break;
                        case "matBTB":
                            batDefaults(5, textValue);
                            break;
                        case "mitTTB":
                            batDefaults(6, textValue);
                            break;
                        case "mitRTB":
                            batDefaults(7, textValue);
                            break;
                        case "matTTB":
                            batDefaults(8, textValue);
                            break;
                        case "matRTB":
                            batDefaults(9, textValue);
                            break;
                        case "serialDTB":
                            serialDefault(textValue);
                            break;
                        case "addressDTB":
                            addressDefault(textValue);
                            break;
                        case "fallDTB":
                            fallDefault(textValue, fallDCB.SelectedIndex);
                            break;
                        case "fallDCB":
                            fallDefault(fallDTB.Text, fallDCB.SelectedIndex);
                            break;
                        case "sensorDCB":
                            sensorDefault(sensorDCB.SelectedIndex, sensorD2CB.SelectedIndex);
                            break;
                        case "sensorD2CB":
                            sensorDefault(sensorDCB.SelectedIndex, sensorD2CB.SelectedIndex);
                            break;
                        case "hitcDTB1":
                            calDefault(textValue, hitcDTB2.Text, hitcDTB3.Text, hitcCB4.SelectedIndex);
                            break;
                        case "hitcDTB2":
                            calDefault(hitcDTB1.Text, textValue, hitcDTB3.Text, hitcCB4.SelectedIndex);
                            break;
                        case "hitcDTB3":
                            calDefault(hitcDTB1.Text, hitcDTB2.Text, textValue, hitcCB4.SelectedIndex);
                            break;
                        case "hitcCB4":
                            calDefault(hitcDTB1.Text, hitcDTB2.Text, hitcDTB3.Text, hitcCB4.SelectedIndex);
                            break;
                        case "mfsCheck":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text);
                            break;
                        case "mfsCB1":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text);
                            break;
                        case "mfsCB2":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text);
                            break;
                        case "mfsCB3":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text);
                            break;
                        case "mfsCB4":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text);
                            break;
                        case "mfsTB1":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text);
                            break;
                        case "mfsTB2":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text);
                            break;
                        case "mfsTB3":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text);
                            break;
                        case "mfsTB4":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text);
                            break;
                        case "mfsTB5":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text);
                            break;
                        case "mglCheck":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text);
                            break;
                        case "mglCB1":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text);
                            break;
                        case "mglCB2":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text);
                            break;
                        case "mglCB3":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text);
                            break;
                        case "mglCB4":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text);
                            break;
                        case "mglTB1":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text);
                            break;
                        case "mglTB2":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text);
                            break;
                        case "mglTB3":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text);
                            break;
                        case "mglTB4":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text);
                            break;
                        case "mglTB5":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text);
                            break;
                        case "phiCheck":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text);
                            break;
                        case "phiCB1":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text);
                            break;
                        case "phiCB2":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text);
                            break;
                        case "phiCB3":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text);
                            break;
                        case "phiCB4":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text);
                            break;
                        case "phiTB1":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text);
                            break;
                        case "phiTB2":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text);
                            break;
                        case "phiTB3":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text);
                            break;
                        case "phiTB4":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text);
                            break;
                        case "phiTB5":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text);
                            break;
                        case "smkCheck":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text);
                            break;
                        case "smkCB1":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text);
                            break;
                        case "smkCB2":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text);
                            break;
                        case "smkCB3":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text);
                            break;
                        case "smkCB4":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text);
                            break;
                        case "smkTB1":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text);
                            break;
                        case "smkTB2":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text);
                            break;
                        case "smkTB3":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text);
                            break;
                        case "smkTB4":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text);
                            break;
                        case "smkTB5":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text);
                            break;
                        case "thmCheck":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text);
                            break;
                        case "thmCB1":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text);
                            break;
                        case "thmCB2":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text);
                            break;
                        case "thmCB3":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text);
                            break;
                        case "thmCB4":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text);
                            break;
                        case "thmTB1":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text);
                            break;
                        case "thmTB2":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text);
                            break;
                        case "thmTB3":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text);
                            break;
                        case "thmTB4":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text);
                            break;
                        case "thmTB5":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text);
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        /**************************************************
         * Sends messages for all the defaults in order
         * to get back the responses.
         * ***********************************************/
        private void batDShowButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("I E SIT");
                conn.sendMessage("I E SAT");
                conn.sendMessage("I E SES");
                conn.sendMessage("I E MIT");
                conn.sendMessage("I E MAT");
                conn.sendMessage("I E TYPE_MIT");
                conn.sendMessage("I E REVERSE_MIT");
                conn.sendMessage("I E TYPE_MAT");
                conn.sendMessage("I E REVERSE_MAT");
                conn.sendMessage("I A");
                conn.sendMessage("I F");
                conn.sendMessage("I G");
                conn.sendMessage("I H");
                conn.sendMessage("I K");
                conn.sendMessage("I N");
                conn.sendMessage("I P");
                conn.sendMessage("I S");
                conn.sendMessage("I T");
                conn.sendMessage("I X");
                logSent("I E SIT");
                logSent("I E SAT");
                logSent("I E SES");
                logSent("I E MIT");
                logSent("I E MAT");
                logSent("I A");
                logSent("I F");
                logSent("I G");
                logSent("I H");
                logSent("I K");
                logSent("I N");
                logSent("I P");
                logSent("I S");
                logSent("I T");
                logSent("I X");
            }
        }

        /**************************************************
         * Sends default battery message
         * ***********************************************/
        public void batDefaults(int index, string battery)
        {
            if (conn != null)
            {
                conn.sendMessage("I E " + index + " " + battery);
                logSent("I E " + index + " " + battery);
            }
        }

        /**************************************************
         * Sends default serial number message
         * ***********************************************/
        public void serialDefault(string serial_num)
        {
            if (conn != null)
            {
                conn.sendMessage("I X " + serial_num);
                logSent("I X " + serial_num);
            }
        }

        /**************************************************
         * Sends default address message
         * ***********************************************/
        public void addressDefault(string address)
        {
            if (conn != null)
            {
                conn.sendMessage("I A " + address);
                logSent("I A " + address);
            }
        }

        /**************************************************
         * Sends default fall parameters message
         * ***********************************************/
        public void fallDefault(String fall1, int fall2)
        {
            if (conn != null)
            {
                conn.sendMessage("I F " + fall1 + " " + Convert.ToString(fall2));
                logSent("I F " + fall1 + " " + Convert.ToString(fall2));
            }
        }

        /**************************************************
         * Sends default hit sensor message
         * ***********************************************/
        public void sensorDefault(int sensor1, int sensor2)
        {
            if (conn != null)
            {
                conn.sendMessage("I S " + Convert.ToString(sensor1) + " " + Convert.ToString(sensor2));
                logSent("I S " + Convert.ToString(sensor1) + " " + Convert.ToString(sensor2));
            }
        }

        /**************************************************
         * Sends default hit calibration message
         * ***********************************************/
        public void calDefault(string cal1, string cal2, string cal3, int cal4)
        {
            if (conn != null)
            {
                conn.sendMessage("I H " + cal1 + " " +  cal2 + " " + cal3 + " " + Convert.ToString(cal4));
                logSent("I H " + cal1 + " " + cal2 + " " + cal3 + " " + Convert.ToString(cal4));
            }
        }

        /**************************************************
         * Sends default MFS message
         * ***********************************************/
        public void mfsDefault(bool mfs1, int mfs2, int mfs3, int mfs4, int mfs5, string mfs6, string mfs7, string mfs8, string mfs9,
                               string mfs10)
        {
            if (conn != null)
            {
                conn.sendMessage("I N " + Convert.ToInt32(mfs1) + " " + mfs2 + " " + mfs3 + " " + mfs4 + " " + mfs5 + " " + mfs6 + " " +
                    " " + mfs7 + " " + mfs8 + " " + mfs9 + " " + mfs10);
                logSent("I N " + Convert.ToInt32(mfs1) + " " + mfs2 + " " + mfs3 + " " + mfs4 + " " + mfs5 + " " + mfs6 + " " +
                    " " + mfs7 + " " + mfs8 + " " + mfs9 + " " + mfs10);
            }
        }

        /**************************************************
         * Sends default MGL message
         * ***********************************************/
        public void mglDefault(bool mgl1, int mgl2, int mgl3, int mgl4, int mgl5, string mgl6, string mgl7, string mgl8, string mgl9,
                       string mgl10)
        {
            if (conn != null)
            {
                conn.sendMessage("I N " + Convert.ToInt32(mgl1) + " " + mgl2 + " " + mgl3 + " " + mgl4 + " " + mgl5 + " " + mgl6 + " " +
                    " " + mgl7 + " " + mgl8 + " " + mgl9 + " " + mgl10);
                logSent("I N " + Convert.ToInt32(mgl1) + " " + mgl2 + " " + mgl3 + " " + mgl4 + " " + mgl5 + " " + mgl6 + " " +
                    " " + mgl7 + " " + mgl8 + " " + mgl9 + " " + mgl10);
            }
        }

        /**************************************************
         * Sends default PHI message
         * ***********************************************/
        public void phiDefault(bool phi1, int phi2, int phi3, int phi4, int phi5, string phi6, string phi7, string phi8, string phi9,
                       string phi10)
        {
            if (conn != null)
            {
                conn.sendMessage("I N " + Convert.ToInt32(phi1) + " " + phi2 + " " + phi3 + " " + phi4 + " " + phi5 + " " + phi6 + " " +
                    " " + phi7 + " " + phi8 + " " + phi9 + " " + phi10);
                logSent("I N " + Convert.ToInt32(phi1) + " " + phi2 + " " + phi3 + " " + phi4 + " " + phi5 + " " + phi6 + " " +
                    " " + phi7 + " " + phi8 + " " + phi9 + " " + phi10);
            }
        }

        /**************************************************
         * Sends default SMK message
         * ***********************************************/
        public void smkDefault(bool smk1, int smk2, int smk3, int smk4, int smk5, string smk6, string smk7, string smk8, string smk9,
                       string smk10)
        {
            if (conn != null)
            {
                conn.sendMessage("I N " + Convert.ToInt32(smk1) + " " + smk2 + " " + smk3 + " " + smk4 + " " + smk5 + " " + smk6 + " " +
                    " " + smk7 + " " + smk8 + " " + smk9 + " " + smk10);
                logSent("I N " + Convert.ToInt32(smk1) + " " + smk2 + " " + smk3 + " " + smk4 + " " + smk5 + " " + smk6 + " " +
                    " " + smk7 + " " + smk8 + " " + smk9 + " " + smk10);
            }
        }

        /**************************************************
         * Sends default THM message
         * ***********************************************/
        public void thmDefault(bool thm1, int thm2, int thm3, int thm4, int thm5, string thm6, string thm7, string thm8, string thm9,
                       string thm10)
        {
            if (conn != null)
            {
                conn.sendMessage("I N " + Convert.ToInt32(thm1) + " " + thm2 + " " + thm3 + " " + thm4 + " " + thm5 + " " + thm6 + " " +
                    " " + thm7 + " " + thm8 + " " + thm9 + " " + thm10);
                logSent("I N " + Convert.ToInt32(thm1) + " " + thm2 + " " + thm3 + " " + thm4 + " " + thm5 + " " + thm6 + " " +
                    " " + thm7 + " " + thm8 + " " + thm9 + " " + thm10);
            }
        }

    }
}
