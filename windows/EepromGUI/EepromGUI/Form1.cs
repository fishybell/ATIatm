using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using System.Text.RegularExpressions;
using System.Diagnostics;
using System.IO;

namespace pmaGUI
{
    public partial class Form1 : Form
    {
        String machine = "";
        String rebootingMachine = "";
        String macPassword = "change MAC";
        Eeprom conn;
        Eeprom bconn;
        String boardType = "";
        String lastMAC = "";
        List<Control> changedList = new List<Control>();
        List<String> ipList = new List<String>();
        //Eeprom listener;
        public delegate void serviceGUIDelegate();
        public Form1()
        {
            InitializeComponent();
            // Populate the drop down list with machines that are broadcasting
            //FindMachines();
        }

        /***************************************************
         * Find the machines that are broadcasting and add
         * them to the drop down list
         * *************************************************/
        private void FindMachines()
        {
            bconn = new Eeprom(
               this,
               delegate(string message, int status)
               {
                   //Find the IP messages of broadcasting machines
                   String[] group = Regex.Split(message, "\n");
                   foreach (var item in group)
                   {
                       if (item != "")
                       {
                           String possibleIP = item.ToString();
                           Console.WriteLine("Received message: " + item);
                           // the first 4 characters are ###.
                           if (Char.IsDigit(possibleIP[0]) && Char.IsDigit(possibleIP[1]) && Char.IsDigit(possibleIP[2]) && possibleIP[3] == '.')
                           {
                               if (!targetCB.Items.Contains(possibleIP))
                               {
                                   errorLBL.Text = "Available Targets";
                                   targetCB.Items.Add(possibleIP);
                                   targetCB.Sorted = true;
                               }
                           }
                       }
                   }
               });
            bconn.StartBroadCastListen();
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
                           //this.logTB.AppendText(machine + " - received: " + item + "\n");
                           //logTB.ScrollToCaret();
                           parseIncoming(item + "\n");
                           parseIP(item);
                       }
                   }
               });
           
            if (conn.StartConnection(machine))
            {
                errorLBL.Text = "";
                showAllButton.Enabled = true;
                setBoardType();
                setMAC();
                setVersion();
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
                disconnect();               
                targetCB.Text = "";
                targetCB.Items.Clear();
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
                rebootingMachine = machine;
                disconnect();
                //timer1.Start();                
                targetCB.Items.Clear();
                targetCB.Text = "";
            }
        }

        private void disconnect()
        {
            rebootingMachine = machine;
            clearFields();
            changedList.Clear();
            conn.CloseConnection();
            conn.killThread();
            //targetCB.Items.Clear();
            //targetCB.Text = "";
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
        * Set the left movement speed
        * ********************************************/
        private void moveLeftButton_Click(object sender, EventArgs e)
        {
            String move = "-" + moveTB.Text;
            if (conn != null)
            {
                conn.sendMessage("M " + move);
                logSent("M " + move);
                stopButton.Focus();
            }
        }

        /***********************************************
        * Set the right movement speed
        * ********************************************/
        private void moveRightButton_Click(object sender, EventArgs e)
        {
            String move = moveTB.Text;
            if (conn != null)
            {
                conn.sendMessage("M " + move);
                logSent("M " + move);
                stopButton.Focus();
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
        * Set the board type as soon as a connection happens
        * ********************************************/
        private void setMAC()
        {
            if (conn != null)
            {
                conn.sendMessage("I M");
                logSent("I M");
            }
        }

        /***********************************************
        * Set the version as soon as a connection happens
        * ********************************************/
        private void setVersion()
        {
            if (conn != null)
            {
                conn.sendMessage("J A");
                logSent("J A");
                conn.sendMessage("J B");
                logSent("J B");
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
            //disableEnable(boardType);
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


        private void accCB0_SelectedIndexChanged(object sender, EventArgs e)
        {

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
            String acc12 = accTB6.Text;
            String acc13 = accTB7.Text;
            String acc14 = accTB8.Text;
            String message = "Q " + acc1 + " " + acc3 + " " + acc4 + " " + acc5 + " " + acc6
                + " " + acc7 + " " + acc8 + " " + acc9 + " " + acc10 + " " + acc11 + " " + acc12
                + " " + acc13 + " " + acc14;
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
                    moveLeftButton.Enabled = false;
                    moveRightButton.Enabled = false;
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
                    posShowButton.Enabled = false;
                    knobShowButton.Enabled = false;
                    modeShowButton.Enabled = false;
                    modeSetButton.Enabled = false;
                    break;
                case "SAT":
                    enableAll();
                    moveShowButton.Enabled = false;
                    moveRightButton.Enabled = false;
                    moveLeftButton.Enabled = false;
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
            moveRightButton.Enabled = true;
            moveLeftButton.Enabled = true;
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
            deviceTB.Text = "";
            macOTB.Text = "";
            moveTB.Text = "";
            speedTB.Text = "";
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
            accTB6.Text = "";
            accTB7.Text = "";
            accTB8.Text = "";
            errorTB.Text = "";
            clearDefaults();
        }

        /******************************************
         * Clears all the fields in the default tab
         * ****************************************
         */
        private void clearDefaults()
        {
            fallDTB.Text = "";
            fallDCB.SelectedIndex = -1;
            bobDCB.SelectedIndex = -1;
            sensorDCB.SelectedIndex = -1;
            sensorD2CB.SelectedIndex = -1;
            hitcDTB1.Text = "";
            hitcDTB2.Text = "";
            hitcDTB3.Text = "";
            hitcCB4.SelectedIndex = -1;
            serialDTB.Text = "";
            addressDTB.Text = "";
            DockDCB.SelectedIndex = -1;
            HomeDCB.SelectedIndex = -1;
            sitBTB.Text = "";
            satBTB.Text = "";
            sesBTB.Text = "";
            mitBTB.Text = "";
            matBTB.Text = "";
            lengthDTB.Text = "";
            revCB.SelectedIndex = -1;
            mfsCheck.Checked = false;
            mfsCB1.SelectedIndex = -1;
            mfsCB2.SelectedIndex = -1;
            mfsCB3.SelectedIndex = -1;
            mfsCB4.SelectedIndex = -1;
            mfsTB1.Text = "";
            mfsTB2.Text = "";
            mfsTB3.Text = "";
            mfsTB4.Text = "";
            mfsTB5.Text = "";
            mfsTB6.Text = "";
            mglCheck.Checked = false;
            mglCB1.SelectedIndex = -1;
            mglCB2.SelectedIndex = -1;
            mglCB3.SelectedIndex = -1;
            mglCB4.SelectedIndex = -1;
            mglTB1.Text = "";
            mglTB2.Text = "";
            mglTB3.Text = "";
            mglTB4.Text = "";
            mglTB5.Text = "";
            mglTB6.Text = "";
            phiCheck.Checked = false;
            phiCB1.SelectedIndex = -1;
            phiCB2.SelectedIndex = -1;
            phiCB3.SelectedIndex = -1;
            phiCB4.SelectedIndex = -1;
            phiTB1.Text = "";
            phiTB2.Text = "";
            phiTB3.Text = "";
            phiTB4.Text = "";
            phiTB5.Text = "";
            phiTB6.Text = "";
            smkCheck.Checked = false;
            smkCB1.SelectedIndex = -1;
            smkCB2.SelectedIndex = -1;
            smkCB3.SelectedIndex = -1;
            smkCB4.SelectedIndex = -1;
            smkTB1.Text = "";
            smkTB2.Text = "";
            smkTB3.Text = "";
            smkTB4.Text = "";
            smkTB5.Text = "";
            smkTB6.Text = "";
            thmCheck.Checked = false;
            thmCB1.SelectedIndex = -1;
            thmCB2.SelectedIndex = -1;
            thmCB3.SelectedIndex = -1;
            thmCB4.SelectedIndex = -1;
            thmTB1.Text = "";
            thmTB2.Text = "";
            thmTB3.Text = "";
            thmTB4.Text = "";
            thmTB5.Text = "";
            thmTB6.Text = "";
            msdCheck.Checked = false;
            msdCB1.SelectedIndex = -1;
            msdCB2.SelectedIndex = -1;
            msdCB3.SelectedIndex = -1;
            msdCB4.SelectedIndex = -1;
            msdTB1.Text = "";
            msdTB2.Text = "";
            msdTB3.Text = "";
            msdTB4.Text = "";
            msdTB5.Text = "";
            msdTB6.Text = "";
            msdTB7.Text = "";
            msdTB8.Text = "";
            freqTB.Text = "";
            lpTB.Text = "";
            hpTB.Text = "";
            radioCheck.Checked = false;
            versionLBL.Text = "";
            version2LBL.Text = "";
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
                //bconn.killBroadCastThread();
                //bconn.CloseConnection();
            }
        }

        private void parseIP(String message)
        {
            // is message an ip address
            // the first 4 characters are ###.
            if (message.Length < 4)
            {
                return;
            }
            if (Char.IsDigit(message[0]) && Char.IsDigit(message[1]) && Char.IsDigit(message[2]) && message[3] == '.') // crashes here when show defaults tab
            {
                if (!targetCB.Items.Contains(message))
                {
                    errorLBL.Text = "Available Targets";
                    targetCB.Items.Add(message);
                    targetCB.Sorted = true;
                }
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
                    logSent("A " + getMessageValue(message, 2));
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
                            logSent("B " + getMessageValue(message, 2));
                            break;
                    }
                    break;
                case 'C':   // target is concealed
                    expSTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    expSTB.Text = "concealed";
                    logSent("C " + getMessageValue(message, 2));
                    break;
                case 'E':   // target is exposed
                    expSTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    expSTB.Text = "exposed";
                    logSent("E " + getMessageValue(message, 2));
                    break;
                case 'F':   // fall parameters
                    String fall = getMessageValue(message, 2);
                    String[] numbs = fall.Split(' ');
                    fkillTB.Text = numbs[0];
                    fallCB.SelectedIndex = Convert.ToInt32(numbs[1]);
                    logSent("F " + getMessageValue(message, 2));
                    break;
                case 'H':   // hit data
                    hitDTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    hitDTB.Text = getMessageValue(message, 2);
                    logSent("H " + getMessageValue(message, 2));
                    break;
                case 'L':   // hit calibration
                    String cal = getMessageValue(message, 2);
                    String[] calSplit = cal.Split(' ');
                    calTB1.Text = calSplit[0];
                    calTB2.Text = calSplit[1];
                    calTB3.Text = calSplit[2];
                    calCB4.SelectedIndex = Convert.ToInt32(calSplit[3]);
                    logSent("L " + getMessageValue(message, 2));
                    break;
                case 'M':   // move speed
                    speedTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    speedTB.Text = getMessageValue(message, 2);
                    logSent("M " + getMessageValue(message, 2));
                    break;
                case 'P':   // sleep status
                    sleepCB.SelectedIndex = Convert.ToInt32(getMessageValue(message, 2));
                    logSent("P " + getMessageValue(message, 2));
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
                    accTB6.Text = accSplit[11];
                    accTB7.Text = accSplit[12];
                    accTB8.Text = accSplit[13];
                    logSent("Q " + getMessageValue(message, 2));
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
                    logSent("S " + getMessageValue(message, 2));
                    break;
                case 'U':   // error messages
                    errorTB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    String eMessage = getMessageValue(message, 2);
                    String[] eSplit = eMessage.Split(' ');
                    errorTB.Text = eMessage;
                    errorTB.Update();
                    logSent("U " + getMessageValue(message, 2));
                    break;
                case 'V':   // current event being called
                    eventCB.ForeColor = System.Drawing.SystemColors.MenuHighlight;
                    String vMessage = getMessageValue(message, 2);
                    String[] vSplit = vMessage.Split(' ');
                    int eventNum = Convert.ToInt32(vSplit[0]);
                    if (eventNum <= 26)
                    {
                        eventCB.SelectedIndex = eventNum;
                    }
                    else
                    {
                        eventCB.SelectedIndex = eventCB.Items.Count-1;
                    }
                    eventCB.Update();
                    logSent("V " + getMessageValue(message, 2));
                    break;
                case 'Y':   // hit sensor
                    String sensor = getMessageValue(message, 2);
                    String[] sensorSplit = sensor.Split(' ');
                    sensorCB.SelectedIndex = Convert.ToInt32(sensorSplit[0]);
                    sensor2CB.SelectedIndex = Convert.ToInt32(sensorSplit[1]);
                    logSent("Y " + getMessageValue(message, 2));
                    break;
                case 'I':   // eeprom board settings
                    switch (second)
                    {
                        case 'A':   // Address
                            addressDTB.Text = getMessageValue(message, 4);
                            addressDTB.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I A " + getMessageValue(message, 4));
                            break;
                        case 'B':   // target type (SIT, MIT, etc.)
                            String board = getMessageValue(message, 4);
                            boardType = board;
                            deviceTB.Text = board;
                            boardCB.SelectedItem = board;
                            // Enable and disable controls according to the board type
                            //disableEnable(boardType);
                            targetCB.Text = machine;
                            logSent("I B " + getMessageValue(message, 4));
                            break;
                        case 'C':   // connection port
                            connectTB.Text = getMessageValue(message, 4);
                            logSent("I C " + getMessageValue(message, 4));
                            break;
                        case 'D':   // communication type
                            commCB.SelectedItem = getMessageValue(message, 4);
                            logSent("I D " + getMessageValue(message, 4));
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
                                        mitBTB.Text = batMovList[1];
                                        mitBTB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        break;
                                    case "MAT":
                                        matBTB.Text = batMovList[1];
                                        matBTB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        break;
                                    case "REVERSE":
                                        if (batMovList[1].Length == 1)
                                        {
                                            revCB.SelectedIndex = Convert.ToInt32(batMovList[1]);
                                            revCB.ForeColor = System.Drawing.SystemColors.WindowText;
                                        }
                                        break;
                                    default:
                                        break;
	                            }
                            logSent("I E " + getMessageValue(message, 4));
                            break;
                        case 'F':   // Fall parameter defaults
                            String[] fallSplit = getMessageValue(message, 4).Split(' ');
                            fallDTB.Text = fallSplit[0];
                            fallDCB.SelectedIndex = Convert.ToInt32(fallSplit[1]);
                            fallDTB.ForeColor = System.Drawing.SystemColors.WindowText;
                            fallDCB.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I F " + getMessageValue(message, 4));
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
                            mglTB6.Text = mglSplit[10];
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
                            mglTB6.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I G " + getMessageValue(message, 4));
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
                            logSent("I H " + getMessageValue(message, 4));
                            break;
                        case 'I':   // IP address
                            ipTB.Text = getMessageValue(message, 4);
                            logSent("I I " + getMessageValue(message, 4));
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
                            smkTB6.Text = smkSplit[10];
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
                            smkTB6.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I K " + getMessageValue(message, 4));
                            break;
                        case 'L':   // listen port
                            listenTB.Text = getMessageValue(message, 4);
                            logSent("I L " + getMessageValue(message, 4));
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
                                // Get last 4 characters of MAC to display
                                macOTB.Text = valid.Substring(12, 5);
                            }
                            logSent("I M " + getMessageValue(message, 4));
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
                            mfsTB6.Text = mfsSplit[10];
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
                            mfsTB6.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I N " + getMessageValue(message, 4));
                            break;
                        case 'O':   // Bob defaults
                            String[] bobSplit = getMessageValue(message, 4).Split(' ');
                            if (bobSplit[0].Length == 1)
                            {
                                bobDCB.SelectedIndex = Convert.ToInt32(bobSplit[0]);   // crashes here when doing show all in defaults
                                bobDCB.ForeColor = System.Drawing.SystemColors.WindowText;
                            }
                            logSent("I O " + getMessageValue(message, 4));
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
                            phiTB6.Text = phiSplit[10];
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
                            phiTB6.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I P " + getMessageValue(message, 4));
                            break;
                        case 'Q':   // Docking end defaults
                            String[] dockSplit = getMessageValue(message, 4).Split(' ');
                            if (dockSplit[0].Length == 1)
                            {
                                DockDCB.SelectedIndex = Convert.ToInt32(dockSplit[0]);
                                DockDCB.ForeColor = System.Drawing.SystemColors.WindowText;
                            }
                            logSent("I Q " + getMessageValue(message, 4));
                            break;
                        case 'S':   // Hit sensor defaults
                            String[] hitSplit = getMessageValue(message, 4).Split(' ');
                            sensorDCB.SelectedIndex = Convert.ToInt32(hitSplit[0]);
                            sensorD2CB.SelectedIndex = Convert.ToInt32(hitSplit[1]);
                            sensorDCB.ForeColor = System.Drawing.SystemColors.WindowText;
                            sensorD2CB.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I S " + getMessageValue(message, 4));
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
                            thmTB6.Text = thmSplit[10];
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
                            thmTB6.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I T " + getMessageValue(message, 4));
                            break;
                        case 'U':   // Radio Frequency Default
                            freqTB.Text = getMessageValue(message, 4);
                            freqTB.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I U " + getMessageValue(message, 4));
                            break;
                        case 'V':   // Radio Low Power Default
                            lpTB.Text = getMessageValue(message, 4);
                            lpTB.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I V " + getMessageValue(message, 4));
                            break;
                        case 'W':   // Radio Frequency Default
                            hpTB.Text = getMessageValue(message, 4);
                            hpTB.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I W " + getMessageValue(message, 4));
                            break;
                        case 'X':   // Serial Number
                            serialDTB.Text = getMessageValue(message, 4);
                            serialDTB.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I X " + getMessageValue(message, 4));
                            break;
                        case 'Y':   // MSD defaults
                            String[] msdSplit = getMessageValue(message, 4).Split(' ');
                            msdCheck.Checked = Convert.ToBoolean(Convert.ToInt32(msdSplit[0]));
                            msdCB1.SelectedIndex = Convert.ToInt32(msdSplit[1]);
                            msdCB2.SelectedIndex = Convert.ToInt32(msdSplit[2]);
                            msdCB3.SelectedIndex = Convert.ToInt32(msdSplit[3]);
                            msdCB4.SelectedIndex = Convert.ToInt32(msdSplit[4]);
                            msdTB1.Text = msdSplit[5];
                            msdTB2.Text = msdSplit[6];
                            msdTB3.Text = msdSplit[7];
                            msdTB4.Text = msdSplit[8];
                            msdTB5.Text = msdSplit[9];
                            msdTB6.Text = msdSplit[10];
                            msdTB7.Text = msdSplit[11];
                            msdTB8.Text = msdSplit[12];
                            msdCheck.ForeColor = System.Drawing.SystemColors.WindowText;
                            msdCB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            msdCB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            msdCB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            msdCB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            msdTB1.ForeColor = System.Drawing.SystemColors.WindowText;
                            msdTB2.ForeColor = System.Drawing.SystemColors.WindowText;
                            msdTB3.ForeColor = System.Drawing.SystemColors.WindowText;
                            msdTB4.ForeColor = System.Drawing.SystemColors.WindowText;
                            msdTB5.ForeColor = System.Drawing.SystemColors.WindowText;
                            msdTB6.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("I Y " + getMessageValue(message, 4));
                            break;
                        case 'Z':   // Home end defaults
                            String[] homeSplit = getMessageValue(message, 4).Split(' ');
                            if (homeSplit[0].Length == 1)
                            {
                                HomeDCB.SelectedIndex = Convert.ToInt32(homeSplit[0]);
                                HomeDCB.ForeColor = System.Drawing.SystemColors.WindowText;
                            }
                            logSent("I Z " + getMessageValue(message, 4));
                            break;
                    }
                    break;
                case 'J':   // eeprom board settings
                    switch (second)
                    {
                        case 'A':   // Major Flash Version
                            versionLBL.Text = getMessageValue(message, 4);
                            logSent("J A " + getMessageValue(message, 4));
                            break;
                        case 'B':   // Minor Flash Version
                            version2LBL.Text = getMessageValue(message, 4);
                            logSent("J B " + getMessageValue(message, 4));
                            break;
                        case 'C':   // Program radio?
                            if (getMessageValue(message, 4) == "Y")
                            {
                                radioCheck.Checked = true;
                            }
                            radioCheck.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("J C " + getMessageValue(message, 4));
                            break;
                        case 'D':   // Track Length
                            lengthDTB.Text = getMessageValue(message, 4);
                            lengthDTB.ForeColor = System.Drawing.SystemColors.WindowText;
                            logSent("J D " + getMessageValue(message, 4));
                            break;
                    }
                    break;
                default:
                    break;
            }
            //changedList.Clear();
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
            logTB.AppendText(machine + " - " + sent + "\n");
            logTB.ScrollToCaret();
        }

        /***************************************************
         * Check to see when a reboot is finished by checking
         * the connection.
         * ************************************************/
        private void timer1_Tick(object sender, EventArgs e)
        {
            if (conn.StartConnection(rebootingMachine))
            {
                errorLBL.Text = "";
                showAllButton.Enabled = true;
                setBoardType();
                setMAC();
                setVersion();
                conn.StartProcess(machine);
                Console.WriteLine("Connected");
                timer1.Stop();
            }
        }

        /**************************************************
         * Events for all text changes in texboxes or
         * index changes for combo boxes in the default tab
         * ***********************************************/
        private void bobDCB_Click(object sender, EventArgs e)
        {
            changedList.Add(bobDCB);
            bobDCB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }


        private void fallDCB_Click(object sender, EventArgs e)
        {
            changedList.Add(fallDCB);
            fallDCB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void fallDTB_Click(object sender, EventArgs e)
        {
            changedList.Add(fallDTB);
            fallDTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void sensorDCB_Click(object sender, EventArgs e)
        {
            changedList.Add(sensorDCB);
            sensorDCB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void sensorD2CB_Click(object sender, EventArgs e)
        {
            changedList.Add(sensorD2CB);
            sensorD2CB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void hitcDTB1_Click(object sender, EventArgs e)
        {
            changedList.Add(hitcDTB1);
            hitcDTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void hitcDTB2_Click(object sender, EventArgs e)
        {
            changedList.Add(hitcDTB2);
            hitcDTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void hitcDTB3_Click(object sender, EventArgs e)
        {
            changedList.Add(hitcDTB3);
            hitcDTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void hitcCB4_Click(object sender, EventArgs e)
        {
            changedList.Add(hitcCB4);
            hitcCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void serialDTB_Click(object sender, EventArgs e)
        {
            changedList.Add(serialDTB);
            serialDTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void addressDTB_Click(object sender, EventArgs e)
        {
            changedList.Add(addressDTB);
            addressDTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void sitBTB_Click(object sender, EventArgs e)
        {
            changedList.Add(sitBTB);
            sitBTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void satBTB_Click(object sender, EventArgs e)
        {
            changedList.Add(satBTB);
            satBTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void sesBTB_Click(object sender, EventArgs e)
        {
            changedList.Add(sesBTB);
            sesBTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mitBTB_Click(object sender, EventArgs e)
        {
            changedList.Add(mitBTB);
            mitBTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void matBTB_Click(object sender, EventArgs e)
        {
            changedList.Add(matBTB);
            matBTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void lengthDTB_Click(object sender, EventArgs e)
        {
            changedList.Add(lengthDTB);
            lengthDTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        } 

        private void DockDCB_Click(object sender, EventArgs e)
        {
            changedList.Add(DockDCB);
            DockDCB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void HomeDCB_Click(object sender, EventArgs e)
        {
            changedList.Add(HomeDCB);
            HomeDCB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void revCB_Click(object sender, EventArgs e)
        {
            changedList.Add(revCB);
            revCB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsCheck_Click(object sender, EventArgs e)
        {
            changedList.Add(mfsCheck);
            mfsCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsCB1_Click(object sender, EventArgs e)
        {
            changedList.Add(mfsCB1);
            mfsCB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsCB2_Click(object sender, EventArgs e)
        {
            changedList.Add(mfsCB2);
            mfsCB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsCB3_Click(object sender, EventArgs e)
        {
            changedList.Add(mfsCB3);
            mfsCB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsCB4_Click(object sender, EventArgs e)
        {
            changedList.Add(mfsCB4);
            mfsCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsTB1_Click(object sender, EventArgs e)
        {
            changedList.Add(mfsTB1);
            mfsTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsTB2_Click(object sender, EventArgs e)
        {
            changedList.Add(mfsTB2);
            mfsTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsTB3_Click(object sender, EventArgs e)
        {
            changedList.Add(mfsTB3);
            mfsTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsTB4_Click(object sender, EventArgs e)
        {
            changedList.Add(mfsTB4);
            mfsTB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsTB5_Click(object sender, EventArgs e)
        {
            changedList.Add(mfsTB5);
            mfsTB5.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mfsTB6_Click(object sender, EventArgs e)
        {
            changedList.Add(mfsTB6);
            mfsTB6.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglCheck_Click(object sender, EventArgs e)
        {
            changedList.Add(mglCheck);
            mglCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglCB1_Click(object sender, EventArgs e)
        {
            changedList.Add(mglCB1);
            mglCB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglCB2_Click(object sender, EventArgs e)
        {
            changedList.Add(mglCB2);
            mglCB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglCB3_Click(object sender, EventArgs e)
        {
            changedList.Add(mglCB3);
            mglCB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglCB4_Click(object sender, EventArgs e)
        {
            changedList.Add(mglCB4);
            mglCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglTB1_Click(object sender, EventArgs e)
        {
            changedList.Add(mglTB1);
            mglTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglTB2_Click(object sender, EventArgs e)
        {
            changedList.Add(mglTB2);
            mglTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglTB3_Click(object sender, EventArgs e)
        {
            changedList.Add(mglTB3);
            mglTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglTB4_Click(object sender, EventArgs e)
        {
            changedList.Add(mglTB4);
            mglTB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglTB5_Click(object sender, EventArgs e)
        {
            changedList.Add(mglTB5);
            mglTB5.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void mglTB6_Click(object sender, EventArgs e)
        {
            changedList.Add(mglTB6);
            mglTB6.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiCheck_Click(object sender, EventArgs e)
        {
            changedList.Add(phiCheck);
            phiCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiCB1_Click(object sender, EventArgs e)
        {
            changedList.Add(phiCB1);
            phiCB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiCB2_Click(object sender, EventArgs e)
        {
            changedList.Add(phiCB2);
            phiCB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiCB3_Click(object sender, EventArgs e)
        {
            changedList.Add(phiCB3);
            phiCB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiCB4_Click(object sender, EventArgs e)
        {
            changedList.Add(phiCB4);
            phiCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiTB1_Click(object sender, EventArgs e)
        {
            changedList.Add(phiTB1);
            phiTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiTB2_Click(object sender, EventArgs e)
        {
            changedList.Add(phiTB2);
            phiTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiTB3_Click(object sender, EventArgs e)
        {
            changedList.Add(phiTB3);
            phiTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiTB4_Click(object sender, EventArgs e)
        {
            changedList.Add(phiTB4);
            phiTB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiTB5_Click(object sender, EventArgs e)
        {
            changedList.Add(phiTB5);
            phiTB5.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void phiTB6_Click(object sender, EventArgs e)
        {
            changedList.Add(phiTB6);
            phiTB6.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkCheck_Click(object sender, EventArgs e)
        {
            changedList.Add(smkCheck);
            smkCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkCB1_Click(object sender, EventArgs e)
        {
            changedList.Add(smkCB1);
            smkCB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkCB2_Click(object sender, EventArgs e)
        {
            changedList.Add(smkCB2);
            smkCB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkCB3_Click(object sender, EventArgs e)
        {
            changedList.Add(smkCB3);
            smkCB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkCB4_Click(object sender, EventArgs e)
        {
            changedList.Add(smkCB4);
            smkCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkTB1_Click(object sender, EventArgs e)
        {
            changedList.Add(smkTB1);
            smkTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkTB2_Click(object sender, EventArgs e)
        {
            changedList.Add(smkTB2);
            smkTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkTB3_Click(object sender, EventArgs e)
        {
            changedList.Add(smkTB3);
            smkTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkTB4_Click(object sender, EventArgs e)
        {
            changedList.Add(smkTB4);
            smkTB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkTB5_Click(object sender, EventArgs e)
        {
            changedList.Add(smkTB5);
            smkTB5.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void smkTB6_Click(object sender, EventArgs e)
        {
            changedList.Add(smkTB6);
            smkTB6.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmCheck_Click(object sender, EventArgs e)
        {
            changedList.Add(thmCheck);
            thmCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmCB1_Click(object sender, EventArgs e)
        {
            changedList.Add(thmCB1);
            thmCB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmCB2_Click(object sender, EventArgs e)
        {
            changedList.Add(thmCB2);
            thmCB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmCB3_Click(object sender, EventArgs e)
        {
            changedList.Add(thmCB3);
            thmCB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmCB4_Click(object sender, EventArgs e)
        {
            changedList.Add(thmCB4);
            thmCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmTB1_Click(object sender, EventArgs e)
        {
            changedList.Add(thmTB1);
            thmTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmTB2_Click(object sender, EventArgs e)
        {
            changedList.Add(thmTB2);
            thmTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmTB3_Click(object sender, EventArgs e)
        {
            changedList.Add(thmTB3);
            thmTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmTB4_Click(object sender, EventArgs e)
        {
            changedList.Add(thmTB4);
            thmTB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmTB5_Click(object sender, EventArgs e)
        {
            changedList.Add(thmTB5);
            thmTB5.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void thmTB6_Click(object sender, EventArgs e)
        {
            changedList.Add(thmTB6);
            thmTB6.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdCheck_Click(object sender, EventArgs e)
        {
            changedList.Add(msdCheck);
            msdCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdCB1_Click(object sender, EventArgs e)
        {
            changedList.Add(msdCB1);
            msdCB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdCB2_Click(object sender, EventArgs e)
        {
            changedList.Add(msdCB2);
            msdCB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdCB3_Click(object sender, EventArgs e)
        {
            changedList.Add(msdCB3);
            msdCB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdCB4_Click(object sender, EventArgs e)
        {
            changedList.Add(msdCB4);
            msdCB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdTB1_Click(object sender, EventArgs e)
        {
            changedList.Add(msdTB1);
            msdTB1.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdTB2_Click(object sender, EventArgs e)
        {
            changedList.Add(msdTB2);
            msdTB2.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdTB3_Click(object sender, EventArgs e)
        {
            changedList.Add(msdTB3);
            msdTB3.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdTB4_Click(object sender, EventArgs e)
        {
            changedList.Add(msdTB4);
            msdTB4.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdTB5_Click(object sender, EventArgs e)
        {
            changedList.Add(msdTB5);
            msdTB5.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdTB6_Click(object sender, EventArgs e)
        {
            changedList.Add(msdTB6);
            msdTB6.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdTB7_Click(object sender, EventArgs e)
        {
            changedList.Add(msdTB7);
            msdTB7.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void msdTB8_Click(object sender, EventArgs e)
        {
            changedList.Add(msdTB8);
            msdTB8.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void freqTB_Click(object sender, EventArgs e)
        {
            changedList.Add(freqTB);
            freqTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void lpTB_Click(object sender, EventArgs e)
        {
            changedList.Add(lpTB);
            lpTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void hpTB_Click(object sender, EventArgs e)
        {
            changedList.Add(hpTB);
            hpTB.ForeColor = System.Drawing.SystemColors.HotTrack;
        }

        private void radioCheck_CheckedChanged(object sender, EventArgs e)
        {
            changedList.Add(radioCheck);
            radioCheck.ForeColor = System.Drawing.SystemColors.HotTrack;
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
                        case "revCB":
                            batDefaults(6, Convert.ToString(revCB.SelectedIndex));
                            break;
                        case "DockDCB":
                            dockDefault(DockDCB.SelectedIndex);
                            break;
                        case "HomeDCB":
                            homeDefault(HomeDCB.SelectedIndex);
                            break;
                        case "serialDTB":
                            serialDefault(textValue);
                            break;
                        case "addressDTB":
                            addressDefault(textValue);
                            break;
                        case "lengthDTB":
                            lengthDefault(textValue);
                            break;
                        case "freqTB":
                            freqDefault(textValue);
                            break;
                        case "lpTB":
                            lpDefault(textValue);
                            break;
                        case "hpTB":
                            hpDefault(textValue);
                            break;
                        case "radioCheck":
                            programDefault(radioCheck.Checked);
                            break;
                        case "fallDTB":
                            fallDefault(textValue, fallDCB.SelectedIndex);
                            break;
                        case "fallDCB":
                            fallDefault(fallDTB.Text, fallDCB.SelectedIndex);
                            break;
                        case "bobDCB":
                            bobDefault(bobDCB.SelectedIndex);
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
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text, mfsTB6.Text);
                            break;
                        case "mfsCB1":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text, mfsTB6.Text);
                            break;
                        case "mfsCB2":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text, mfsTB6.Text);
                            break;
                        case "mfsCB3":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text, mfsTB6.Text);
                            break;
                        case "mfsCB4":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text, mfsTB6.Text);
                            break;
                        case "mfsTB1":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text, mfsTB6.Text);
                            break;
                        case "mfsTB2":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text, mfsTB6.Text);
                            break;
                        case "mfsTB3":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text, mfsTB6.Text);
                            break;
                        case "mfsTB4":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text, mfsTB6.Text);
                            break;
                        case "mfsTB5":
                            mfsDefault(mfsCheck.Checked, mfsCB1.SelectedIndex, mfsCB2.SelectedIndex, mfsCB3.SelectedIndex,
                                mfsCB4.SelectedIndex, mfsTB1.Text, mfsTB2.Text, mfsTB3.Text, mfsTB4.Text, mfsTB5.Text, mfsTB6.Text);
                            break;
                        case "mglCheck":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text, mfsTB6.Text);
                            break;
                        case "mglCB1":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text, mglTB6.Text);
                            break;
                        case "mglCB2":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text, mglTB6.Text);
                            break;
                        case "mglCB3":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text, mglTB6.Text);
                            break;
                        case "mglCB4":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text, mglTB6.Text);
                            break;
                        case "mglTB1":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text, mglTB6.Text);
                            break;
                        case "mglTB2":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text, mglTB6.Text);
                            break;
                        case "mglTB3":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text, mglTB6.Text);
                            break;
                        case "mglTB4":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text, mglTB6.Text);
                            break;
                        case "mglTB5":
                            mglDefault(mglCheck.Checked, mglCB1.SelectedIndex, mglCB2.SelectedIndex, mglCB3.SelectedIndex,
                                mglCB4.SelectedIndex, mglTB1.Text, mglTB2.Text, mglTB3.Text, mglTB4.Text, mglTB5.Text, mglTB6.Text);
                            break;
                        case "phiCheck":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text, phiTB6.Text);
                            break;
                        case "phiCB1":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text, phiTB6.Text);
                            break;
                        case "phiCB2":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text, phiTB6.Text);
                            break;
                        case "phiCB3":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text, phiTB6.Text);
                            break;
                        case "phiCB4":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text, phiTB6.Text);
                            break;
                        case "phiTB1":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text, phiTB6.Text);
                            break;
                        case "phiTB2":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text, phiTB6.Text);
                            break;
                        case "phiTB3":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text, phiTB6.Text);
                            break;
                        case "phiTB4":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text, phiTB6.Text);
                            break;
                        case "phiTB5":
                            phiDefault(phiCheck.Checked, phiCB1.SelectedIndex, phiCB2.SelectedIndex, phiCB3.SelectedIndex,
                                phiCB4.SelectedIndex, phiTB1.Text, phiTB2.Text, phiTB3.Text, phiTB4.Text, phiTB5.Text, phiTB6.Text);
                            break;
                        case "smkCheck":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text, smkTB6.Text);
                            break;
                        case "smkCB1":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text, smkTB6.Text);
                            break;
                        case "smkCB2":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text, smkTB6.Text);
                            break;
                        case "smkCB3":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text, smkTB6.Text);
                            break;
                        case "smkCB4":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text, smkTB6.Text);
                            break;
                        case "smkTB1":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text, smkTB6.Text);
                            break;
                        case "smkTB2":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text, smkTB6.Text);
                            break;
                        case "smkTB3":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text, smkTB6.Text);
                            break;
                        case "smkTB4":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text, smkTB6.Text);
                            break;
                        case "smkTB5":
                            smkDefault(smkCheck.Checked, smkCB1.SelectedIndex, smkCB2.SelectedIndex, smkCB3.SelectedIndex,
                                smkCB4.SelectedIndex, smkTB1.Text, smkTB2.Text, smkTB3.Text, smkTB4.Text, smkTB5.Text, smkTB6.Text);
                            break;
                        case "thmCheck":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text, thmTB6.Text);
                            break;
                        case "thmCB1":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text, thmTB6.Text);
                            break;
                        case "thmCB2":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text, thmTB6.Text);
                            break;
                        case "thmCB3":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text, thmTB6.Text);
                            break;
                        case "thmCB4":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text, thmTB6.Text);
                            break;
                        case "thmTB1":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text, thmTB6.Text);
                            break;
                        case "thmTB2":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text, thmTB6.Text);
                            break;
                        case "thmTB3":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text, thmTB6.Text);
                            break;
                        case "thmTB4":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text, thmTB6.Text);
                            break;
                        case "thmTB5":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text, thmTB6.Text);
                            break;
                        case "thmTB6":
                            thmDefault(thmCheck.Checked, thmCB1.SelectedIndex, thmCB2.SelectedIndex, thmCB3.SelectedIndex,
                                thmCB4.SelectedIndex, thmTB1.Text, thmTB2.Text, thmTB3.Text, thmTB4.Text, thmTB5.Text, thmTB6.Text);
                            break;
                        case "msdCheck":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdCB1":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdCB2":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdCB3":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdCB4":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdTB1":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdTB2":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdTB3":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdTB4":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdTB5":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdTB6":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdTB7":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        case "msdTB8":
                            msdDefault(msdCheck.Checked, msdCB1.SelectedIndex, msdCB2.SelectedIndex, msdCB3.SelectedIndex,
                                msdCB4.SelectedIndex, msdTB1.Text, msdTB2.Text, msdTB3.Text, msdTB4.Text, msdTB5.Text, msdTB6.Text, msdTB7.Text, msdTB8.Text);
                            break;
                        default:
                            break;
                    }
                }
                changedList.Clear();
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
                conn.sendMessage("I E REVERSE");
                conn.sendMessage("I A");
                conn.sendMessage("I F");
                conn.sendMessage("I G");
                conn.sendMessage("I H");
                conn.sendMessage("I K");
                conn.sendMessage("I N");
                conn.sendMessage("I O");
                conn.sendMessage("I P");
                conn.sendMessage("I Q");
                conn.sendMessage("I S");
                conn.sendMessage("I T");
                conn.sendMessage("I U");
                conn.sendMessage("I V");
                conn.sendMessage("I W");
                conn.sendMessage("I X");
                conn.sendMessage("I Y");
                conn.sendMessage("I Z");
                conn.sendMessage("J C");
                conn.sendMessage("J D");
                logSent("I E SIT");
                logSent("I E SAT");
                logSent("I E SES");
                logSent("I E MIT");
                logSent("I E MAT");
                logSent("I E REVERSE");
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
                logSent("J C");
                logSent("J D");
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

        /************************************************
         * Sends whether or not to reprogram the battery
         * **********************************************/
        public void programDefault(Boolean state)
        {
            string value = "";
            if (state == true) 
            { 
                value = "Y"; 
            } else {
                value = "N";
            }
            if (conn != null)
            {
                conn.sendMessage("J C " + value);
                logSent("J C " + value);
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
         * Sends default track length message
         * ***********************************************/
        public void lengthDefault(string length)
        {
            if (conn != null)
            {
                conn.sendMessage("J D " + length);
                logSent("J D " + length);
            }
        }

        /**************************************************
        * Sends default radio frequncy message
        * ***********************************************/
        public void freqDefault(string frequency)
        {
            if (conn != null)
            {
                conn.sendMessage("I U " + frequency);
                logSent("I U " + frequency);
            }
        }

        /**************************************************
         * Sends default radio low power message
        * ***********************************************/
        public void lpDefault(string low_power)
        {
            if (conn != null)
            {
                conn.sendMessage("I V " + low_power);
                logSent("I V " + low_power);
            }
        }

        /**************************************************
        * Sends default radio high power message
        * ***********************************************/
        public void hpDefault(string high_power)
        {
            if (conn != null)
            {
                conn.sendMessage("I W " + high_power);
                logSent("I W " + high_power);
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
        * Sends default docking end message
        * ***********************************************/
        public void dockDefault(int dock)
        {
            if (conn != null)
            {
                conn.sendMessage("I Q " + Convert.ToString(dock));
                logSent("I Q " + Convert.ToString(dock));
            }
        }

        /**************************************************
        * Sends default home end message
        * ***********************************************/
        public void homeDefault(int dock)
        {
            if (conn != null)
            {
                conn.sendMessage("I Z " + Convert.ToString(dock));
                logSent("I Z " + Convert.ToString(dock));
            }
        }

        /**************************************************
        * Sends default bob type message
        * ***********************************************/
        public void bobDefault(int bob1)
        {
            if (conn != null)
            {
                conn.sendMessage("I O " + Convert.ToString(bob1));
                logSent("I O " + Convert.ToString(bob1));
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
                               string mfs10, string mfs11)
        {
            if (conn != null)
            {
                conn.sendMessage("I N " + Convert.ToInt32(mfs1) + " " + mfs2 + " " + mfs3 + " " + mfs4 + " " + mfs5 + " " + mfs6 + " " +
                    mfs7 + " " + mfs8 + " " + mfs9 + " " + mfs10 + " " + mfs11 + " " + "0" + " " + "0");
                logSent("I N " + Convert.ToInt32(mfs1) + " " + mfs2 + " " + mfs3 + " " + mfs4 + " " + mfs5 + " " + mfs6 + " " +
                    mfs7 + " " + mfs8 + " " + mfs9 + " " + mfs10 + " " + mfs11 + " " + "0" + " " + "0");
            }
        }

        /**************************************************
         * Sends default MGL message
         * ***********************************************/
        public void mglDefault(bool mgl1, int mgl2, int mgl3, int mgl4, int mgl5, string mgl6, string mgl7, string mgl8, string mgl9,
                       string mgl10, string mgl11)
        {
            if (conn != null)
            {
                conn.sendMessage("I G " + Convert.ToInt32(mgl1) + " " + mgl2 + " " + mgl3 + " " + mgl4 + " " + mgl5 + " " + mgl6 + " " +
                    mgl7 + " " + mgl8 + " " + mgl9 + " " + mgl10 + " " + mgl11 + " " + "0" + " " + "0");
                logSent("I G " + Convert.ToInt32(mgl1) + " " + mgl2 + " " + mgl3 + " " + mgl4 + " " + mgl5 + " " + mgl6 + " " +
                    mgl7 + " " + mgl8 + " " + mgl9 + " " + mgl10 + " " + mgl11 + " " + "0" + " " + "0");
            }
        }

        /**************************************************
         * Sends default PHI message
         * ***********************************************/
        public void phiDefault(bool phi1, int phi2, int phi3, int phi4, int phi5, string phi6, string phi7, string phi8, string phi9,
                       string phi10, string phi11)
        {
            if (conn != null)
            {
                conn.sendMessage("I P " + Convert.ToInt32(phi1) + " " + phi2 + " " + phi3 + " " + phi4 + " " + phi5 + " " + phi6 + " " +
                    phi7 + " " + phi8 + " " + phi9 + " " + phi10 + " " + phi11 + " " + "0" + " " + "0");
                logSent("I P " + Convert.ToInt32(phi1) + " " + phi2 + " " + phi3 + " " + phi4 + " " + phi5 + " " + phi6 + " " +
                    phi7 + " " + phi8 + " " + phi9 + " " + phi10 + " " + phi11 + " " + "0" + " " + "0");
            }
        }

        /**************************************************
         * Sends default SMK message
         * ***********************************************/
        public void smkDefault(bool smk1, int smk2, int smk3, int smk4, int smk5, string smk6, string smk7, string smk8, string smk9,
                       string smk10, string smk11)
        {
            if (conn != null)
            {
                conn.sendMessage("I K " + Convert.ToInt32(smk1) + " " + smk2 + " " + smk3 + " " + smk4 + " " + smk5 + " " + smk6 + " " +
                    smk7 + " " + smk8 + " " + smk9 + " " + smk10 + " " + smk11 + " " + "0" + " " + "0");
                logSent("I K " + Convert.ToInt32(smk1) + " " + smk2 + " " + smk3 + " " + smk4 + " " + smk5 + " " + smk6 + " " +
                    smk7 + " " + smk8 + " " + smk9 + " " + smk10 + " " + smk11 + " " + "0" + " " + "0");
            }
        }

        /**************************************************
         * Sends default THM message
         * ***********************************************/
        public void thmDefault(bool thm1, int thm2, int thm3, int thm4, int thm5, string thm6, string thm7, string thm8, string thm9,
                       string thm10, string thm11)
        {
            if (conn != null)
            {
                conn.sendMessage("I T " + Convert.ToInt32(thm1) + " " + thm2 + " " + thm3 + " " + thm4 + " " + thm5 + " " + thm6 + " " +
                    thm7 + " " + thm8 + " " + thm9 + " " + thm10 + " " + thm11 + " " + "0" + " " + "0");
                logSent("I T " + Convert.ToInt32(thm1) + " " + thm2 + " " + thm3 + " " + thm4 + " " + thm5 + " " + thm6 + " " +
                    thm7 + " " + thm8 + " " + thm9 + " " + thm10 + " " + thm11 + " " + "0" + " " + "0");
            }
        }

        /**************************************************
        * Sends default MSD message
        * ***********************************************/
        public void msdDefault(bool msd1, int msd2, int msd3, int msd4, int msd5, string msd6, string msd7, string msd8, string msd9,
                       string msd10, string msd11, string msd12, string msd13)
        {
            if (conn != null)
            {
                conn.sendMessage("I Y " + Convert.ToInt32(msd1) + " " + msd2 + " " + msd3 + " " + msd4 + " " + msd5 + " " + msd6 + " " +
                    msd7 + " " + msd8 + " " + msd9 + " " + msd10 + " " + msd11);
                logSent("I Y " + Convert.ToInt32(msd1) + " " + msd2 + " " + msd3 + " " + msd4 + " " + msd5 + " " + msd6 + " " +
                    msd7 + " " + msd8 + " " + msd9 + " " + msd10 + " " + msd11 + " " + msd12 + " " + msd13);
            }
        }

        private void scan_button_Click(object sender, EventArgs e)
        {
            FindMachines();
            scan_button.Enabled = false;
        }

        private void clear_button_Click(object sender, EventArgs e)
        {
           targetCB.Items.Clear();
        }

        private void VersionViewButton_Click(object sender, EventArgs e)
        {
            if (conn != null)
            {
                conn.sendMessage("J A");
                conn.sendMessage("J B");
                logSent("J A");
                logSent("J B");
            }
        }

        private void firmButton_Click(object sender, EventArgs e)
        {
            ProcessStartInfo psi = new ProcessStartInfo();
            OpenFileDialog fileDialog = new OpenFileDialog();
            OpenFileDialog openDialog = new OpenFileDialog();
            
            // Grab the file info
            if (openDialog.ShowDialog() == DialogResult.OK)
            {
                String batFile = ".\\atifirmwarecopy.bat";

                psi.FileName = batFile;
                String shellFile = openDialog.SafeFileName;
                psi.Arguments = machine + " " + openDialog.FileName + " " + shellFile + " root shoot";

                // Hides the console window that would pop up
                //psi.WindowStyle = ProcessWindowStyle.Hidden;

                // Create new process and set the starting information
                Process p = new Process();
                p.StartInfo = psi;

                // Lets you know when the process has been completed
                p.EnableRaisingEvents = true;
                p.Start();

                // Wait until the process has completed
                while (!p.HasExited)
                {
                    System.Threading.Thread.Sleep(1000);
                }
                // Check to see what the exit code was
                if (p.ExitCode != 0)
                {
                    Console.WriteLine("There was an error with the file.");
                }
                else
                {
                    setVersion();
                    disconnect();
                    targetCB.Text = "";
                    targetCB.Items.Clear();
                }
            }
            

        }

        private void radioButton_Click(object sender, EventArgs e)
        {
            ProcessStartInfo psi = new ProcessStartInfo();

            String radioFile = ".\\radiotest.bat";

            psi.FileName = radioFile;
            psi.Arguments = machine + " shoot root";

            // Create new process and set the starting information
            Process p = new Process();
            p.StartInfo = psi;

            // Lets you know when the process has been completed
            p.EnableRaisingEvents = true;
            p.Start();

            // Wait until the process has completed
            while (!p.HasExited)
            {
                System.Threading.Thread.Sleep(1000);
            }
            // Check to see what the exit code was
            if (p.ExitCode != 0)
            {
                Console.WriteLine("There was an error with the file.");
            }
        }
 
    }
}
