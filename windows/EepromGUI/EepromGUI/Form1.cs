using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;

namespace EepromGUI
{
    public partial class Form1 : Form
    {
        String machine = "sam5";
        String macPassword = "change MAC";
        Server conn;
        Server listener;
        public delegate void serviceGUIDelegate();
        public Form1()
        {
            conn = new Server();
            conn.StartConnection(machine);
            InitializeComponent();
            //targetCB.SelectedItem = machine;
            disableEnable(getBoardType());
            //Start listener
            listener = new Server(
                this,
                delegate(string message, int status)
                {
                    this.logTB.AppendText(message);
                });
            listener.StartProcess(machine);
        }

        private void targetCB_SelectedIndexChanged(object sender, EventArgs e)
        {
            conn.CloseConnection();
            machine = (string)targetCB.SelectedItem;
            conn.StartConnection(machine);
            disableEnable(getBoardType());
        }

        private void batGetButton_Click(object sender, EventArgs e)
        {
            //String battery = Server.ConnectEeprom("B", machine);
            String battery = conn.sendMessage("B");
            if (battery.StartsWith("B"))
            {
                int newline = battery.IndexOf("\n");
                battery = battery.Substring(2, newline-2);  // grab just the battery value of the string
                batTB.Text = battery;
            }
        }

        private void concealButton_Click(object sender, EventArgs e)
        {
            //Server.ConnectEeprom("C", machine);
            conn.sendMessage("C");
        }

        // Expose the device
        private void exposeButton_Click(object sender, EventArgs e)
        {
            //Server.ConnectEeprom("E", machine);
            conn.sendMessage("E");
        }

        // Shutdown Device
        private void shutdownButton_Click(object sender, EventArgs e)
        {
            //Server.ConnectEeprom("K", machine);
            conn.sendMessage("K");
        }

        // Emergency Stop
        private void stopButton_Click(object sender, EventArgs e)
        {
            //Server.ConnectEeprom("X", machine);
            conn.sendMessage("X");
        }

        // Reboot the machine
        private void rebootButton_Click(object sender, EventArgs e)
        {
            //Server.ConnectEeprom("I R", machine);
            conn.sendMessage("I R");
        }

        // Show the move speed
        private void moveShowButton_Click(object sender, EventArgs e)
        {
            //String move = Server.ConnectEeprom("M", machine);
            String move = conn.sendMessage("M");
            if (move.StartsWith("M"))
            {
                int newline = move.IndexOf("\n");
                move = move.Substring(2, newline - 2);  // grab just the move value of the string
                moveTB.Text = move;
            }
        }

        private void moveSetButton_Click(object sender, EventArgs e)
        {
            String move = moveTB.Text;
            // Set the mover speed
            //Server.ConnectEeprom("M " + move, machine);
            conn.sendMessage("M " + move);
        }

        // Show the hit data
        private void hitDShowButton_Click(object sender, EventArgs e)
        {
            //String hit_data = Server.ConnectEeprom("H", machine);
            String hit_data = conn.sendMessage("H");
            if (hit_data.StartsWith("H"))
            {
                int newline = hit_data.IndexOf("\n");
                hit_data = hit_data.Substring(2, newline - 2);  // grab just the hit data of the string
                hitDTB.Text = hit_data;
            }
        }

        // Set the hit data
        private void hitDSetButton_Click(object sender, EventArgs e)
        {
            String hit_data = hitDTB.Text;
            // Set the hit_data
            //Server.ConnectEeprom("H " + hit_data, machine);
            conn.sendMessage("H " + hit_data);
        }

        private void toggleButton_Click(object sender, EventArgs e)
        {
            //Server.ConnectEeprom("T", machine);
            conn.sendMessage("T");
        }

        // Show the sleep status
        private void sleepShowButton_Click(object sender, EventArgs e)
        {
            //String sleep = Server.ConnectEeprom("P", machine);
            String sleep = conn.sendMessage("P");
            if (sleep.StartsWith("P"))
            {
                int newline = sleep.IndexOf("\n");
                sleep = sleep.Substring(2, newline - 2);
                sleepCB.SelectedIndex = Convert.ToInt32(sleep);
            }
        }

        // Set the sleep status
        private void sleepSetButton_Click(object sender, EventArgs e)
        {
            int sleep = sleepCB.SelectedIndex;
            // Set the sleep type
            //Server.ConnectEeprom("P " + sleep, machine);
            conn.sendMessage("P");
        }

        // Show the board type
        private void boardShowButton_Click(object sender, EventArgs e)
        {
            getBoardType();
        }

        private String getBoardType()
        {
            //String board = Server.ConnectEeprom("I B", machine);
            String board = conn.sendMessage("I B");
            if (board.StartsWith("I B"))
            {
                int newline = board.IndexOf("\n");
                board = board.Substring(4, newline - 4);
                boardCB.SelectedItem = board;
                deviceTB.Text = board;
            }
            return board;
        }

        // Set the board type
        private void boardSetButton_Click(object sender, EventArgs e)
        {
            String board = (String)boardCB.SelectedItem;
            //Server.ConnectEeprom("I B " + board, machine);
            conn.sendMessage("I B " + board);
            disableEnable(getBoardType());
        }

        // Show the communication type
        private void commShowButton_Click(object sender, EventArgs e)
        {
            //String comm = Server.ConnectEeprom("I D", machine);
            String comm = conn.sendMessage("I D");
            if (comm.StartsWith("I D"))
            {
                int newline = comm.IndexOf("\n");
                comm = comm.Substring(4, newline - 4);
                commCB.SelectedItem = comm;
            }
        }

        // Set the communication type
        private void commSetButton_Click(object sender, EventArgs e)
        {
            String comm = (String)commCB.SelectedItem;
            //Server.ConnectEeprom("I D " + comm, machine);
            conn.sendMessage("I D " + comm);
        }

        // Show the MAC address
        private void macShowButton_Click(object sender, EventArgs e)
        {
            //String mac = Server.ConnectEeprom("I M", machine);
            String mac = conn.sendMessage("I M");
            if (mac.StartsWith("I M"))
            {
                int newline = mac.IndexOf("\n");
                mac = mac.Substring(4, newline - 4);
                macTB.Text = mac;
            }
            else
            {
                Console.WriteLine("log " + mac);
            }
        }

        // Set the MAC address
        private void macSetButton_Click(object sender, EventArgs e)
        {
            // Make a password popup box
            passwordPanel.Visible = true;
            passLabel1.Text = "Call Action Target customer service";
            passLabel2.Text = "for the password.";
        }

        // Checks to see if the password is correct to change the address
        private void okButton_Click(object sender, EventArgs e)
        {
            if (passwordTB.Text == macPassword)
            {
                String mac = macTB.Text;
                // Set the mac adress
                //String valid = Server.ConnectEeprom("I M " + mac, machine);
                String valid = conn.sendMessage("I M " + mac);
                if (valid == "Invalid MAC address")
                {
                    passLabel1.Text = "";
                    passLabel2.Text = "Invalid MAC address";
                }
                else
                {
                    passwordPanel.Visible = false;
                }
            }
            else
            {
                passLabel1.Text = "";
                passLabel2.Text = "Incorrect Password";
            }
        }

        // Hides the password panel
        private void cancelButton_Click(object sender, EventArgs e)
        {
            passwordPanel.Visible = false;
        }



        // Show the listen port number
        private void listenShowButton_Click(object sender, EventArgs e)
        {
            //String listen = Server.ConnectEeprom("I L", machine);
            String listen = conn.sendMessage("I L");
            if (listen.StartsWith("I L"))
            {
                int newline = listen.IndexOf("\n");
                listen = listen.Substring(4, newline - 4);
                listenTB.Text = listen;
            }
        }

        // Set the listen port number
        private void listenSetButton_Click(object sender, EventArgs e)
        {
            String listen = listenTB.Text;
            //Server.ConnectEeprom("I L " + listen, machine);
            conn.sendMessage("I L " + listen);
        }

        // Show the connect port number
        private void connectShowButton_Click(object sender, EventArgs e)
        {
            //String connect = Server.ConnectEeprom("I C", machine);
            String connect = conn.sendMessage("I C");
            if (connect.StartsWith("I C"))
            {
                int newline = connect.IndexOf("\n");
                connect = connect.Substring(4, newline - 4);
                connectTB.Text = connect;
            }
        }

        // Set the connect port number
        private void connectSetButton_Click(object sender, EventArgs e)
        {
            String connect = connectTB.Text;
            //Server.ConnectEeprom("I C " + connect, machine);
            conn.sendMessage("I C " + connect);
        }

        // Show the IP address
        private void ipShowButton_Click(object sender, EventArgs e)
        {
            //String ip = Server.ConnectEeprom("I I", machine);
            String ip = conn.sendMessage("I I");
            if (ip.StartsWith("I I"))
            {
                int newline = ip.IndexOf("\n");
                ip = ip.Substring(4, newline - 4);
                ipTB.Text = ip;
            }
        }

        // Set the IP address
        private void ipSetButton_Click(object sender, EventArgs e)
        {
            String ip = ipTB.Text;
            //Server.ConnectEeprom("I I " + ip, machine);
            conn.sendMessage("I I " + ip);
        }

        // Show the fall parameters
        private void fallShowButton_Click(object sender, EventArgs e)
        {
            //String fall = Server.ConnectEeprom("F", machine);
            String fall = conn.sendMessage("F");
            if (fall.StartsWith("F"))
            {
                int newline = fall.IndexOf("\n");
                fall = fall.Substring(2, newline - 2);
                String[] numbs = fall.Split(' ');
                fkillTB.Text = numbs[0];
                fallCB.SelectedIndex = Convert.ToInt32(numbs[1]);
            }
        }

        // Set the fall parameters
        private void fallSetButton_Click(object sender, EventArgs e)
        {
            String fKill = fkillTB.Text;
            int fParams = fallCB.SelectedIndex;
            // Set the hits to kill and the fall parameters to go by
            //Server.ConnectEeprom("F " + fKill + " " + fParams, machine);
            conn.sendMessage("F " + fKill + " " + fParams);
        }

        // Send event to the kernal
        private void eventButton_Click(object sender, EventArgs e)
        {
            int sendEvent = eventCB.SelectedIndex;
            //Server.ConnectEeprom("V " + sendEvent, machine);
            conn.sendMessage("V " + sendEvent);
        }

        // Show the hit sensor settings
        private void sensorShowButton_Click(object sender, EventArgs e)
        {
            //String sensor = Server.ConnectEeprom("Y", machine);
            String sensor = conn.sendMessage("Y");
            if (sensor.StartsWith("Y"))
            {
                int newline = sensor.IndexOf("\n");
                sensor = sensor.Substring(2, newline - 2);
                String[] numbs = sensor.Split(' ');
                sensorCB.SelectedIndex = Convert.ToInt32(numbs[0]);
                sensor2CB.SelectedIndex = Convert.ToInt32(numbs[1]);
            }
        }

        // Set the hit sensor settings
        private void sensorSetButton_Click(object sender, EventArgs e)
        {
            int sensor1 = sensorCB.SelectedIndex;
            int sensor2 = sensor2CB.SelectedIndex;
            //Server.ConnectEeprom("Y " + sensor1 + " " + sensor2, machine);
            conn.sendMessage("Y " + sensor1 + " " + sensor2);
        }

        // Show the hit calibration
        private void calShowButton_Click(object sender, EventArgs e)
        {
            //String cal = Server.ConnectEeprom("L", machine);
            String cal = conn.sendMessage("L");
            if (cal.StartsWith("L"))
            {
                int newline = cal.IndexOf("\n");
                cal = cal.Substring(2, newline - 2);
                String[] numbs = cal.Split(' ');
                calTB1.Text = numbs[0];
                calTB2.Text = numbs[1];
                calTB3.Text = numbs[2];
                calTB4.Text = numbs[3];
            }
        }

        // Set the hit calibration
        private void calSetButton_Click(object sender, EventArgs e)
        {
            int cal1 = Convert.ToInt32(calTB1.Text);
            int cal2 = Convert.ToInt32(calTB2.Text);
            int cal3 = Convert.ToInt32(calTB3.Text);
            int cal4 = Convert.ToInt32(calTB4.Text);
            // Set the hit calibration
            //Server.ConnectEeprom("L " + cal1 + " " + cal2 + " " + cal3 + " " + cal4, machine);
            conn.sendMessage("L " + cal1 + " " + cal2 + " " + cal3 + " " + cal4);
        }

        // Show the accessory details
        private void accShowButton_Click(object sender, EventArgs e)
        {
            //String acc = Server.ConnectEeprom("Q", machine);
            String acc = conn.sendMessage("Q");
            if (acc.StartsWith("Q"))
            {
                int newline = acc.IndexOf("\n");
                acc = acc.Substring(2, newline - 2);
                String[] numbs = acc.Split(' ');
                accCB0.SelectedItem = numbs[0];
                // Exists = 1
                if (Convert.ToInt32(numbs[1]) == 1)
                {
                    accTB0.Text = "Exists";
                }
                else
                {
                    accTB0.Text = "Doesn't Exist";
                }
                accCB2.SelectedIndex = Convert.ToInt32(numbs[2]);
                accCB3.SelectedIndex = Convert.ToInt32(numbs[3]);
                accCB4.SelectedIndex = Convert.ToInt32(numbs[4]);
                accCB5.SelectedIndex = Convert.ToInt32(numbs[5]);
                accTB1.Text = numbs[6];
                accTB2.Text = numbs[7];
                accTB3.Text = numbs[8];
                accTB4.Text = numbs[9];
                accTB5.Text = numbs[10];
            }
        }

        // Set the accessory details
        private void accSetButton_Click(object sender, EventArgs e)
        {
            String acc1 = (string)accCB0.SelectedItem;
            //int acc2 = (int)accCB1.SelectedIndex; // it automatically checks to see if the accessory exists
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
            //Server.ConnectEeprom(message, machine);
            conn.sendMessage(message);
        }

        // Get the position
        private void posShowButton_Click(object sender, EventArgs e)
        {
            //String position = Server.ConnectEeprom("A", machine);
            String position = conn.sendMessage("A");
            if (position.StartsWith("A"))
            {
                int newline = position.IndexOf("\n");
                position = position.Substring(2, newline - 2);  // grab just the position value of the string
                posTB.Text = position;
            }
        }

        // Show the exposure status
        private void expSShowButton_Click(object sender, EventArgs e)
        {
            //String status = Server.ConnectEeprom("S", machine);
            String status = conn.sendMessage("S");
            if (status.StartsWith("S"))
            {
                int newline = status.IndexOf("\n");
                status = status.Substring(2, newline - 2);  // grab just the status value of the string
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
                
            }
        }

        // Get the GPS location
        private void gpsShowButton_Click(object sender, EventArgs e)
        {
            //String gps = Server.ConnectEeprom("G", machine);
            String gps = conn.sendMessage("G");
            if (gps.StartsWith("G"))
            {
                int newline = gps.IndexOf("\n");
                gps = gps.Substring(2, newline - 2);  // grab just the gps value of the string
                posTB.Text = gps;
            }
        }

        // Show the knob information
        private void knobShowButton_Click(object sender, EventArgs e)
        {
            //String knob = Server.ConnectEeprom("Z", machine);
            String knob = conn.sendMessage("Z");
            if (knob.StartsWith("BIT"))
            {
                int newline = knob.IndexOf("\n");
                knob = knob.Substring(0, newline);  // grab just the knob value of the string
                String[] numbs = knob.Split(' ');
                knobTB.Text = numbs[0] + " " + numbs[1];
                if (Convert.ToInt32(numbs[2]) == 0)
                {
                    knobTB2.Text = "off";
                }
                else
                {
                    knobTB2.Text = "on";
                }
            }
        }

        // Show the SES mode
        private void modeShowButton_Click(object sender, EventArgs e)
        {
            //String mode = Server.ConnectEeprom("O", machine);
            String mode = conn.sendMessage("O");
            if (mode.StartsWith("BIT"))
            {
                int newline = mode.IndexOf("\n");
                mode = mode.Substring(0, newline);  // grab just the mode value of the string
                String[] numbs = mode.Split(' ');
                modeTB.Text = numbs[2];
            }
        }

        // Set the SES mode
        private void modeSetButton_Click(object sender, EventArgs e)
        {
            String mode = modeTB.Text;
            // Set the ses mode
            //Server.ConnectEeprom("O " + mode, machine);
            conn.sendMessage("O " + mode);
        }

        // Show full hit data
        private void fHitShowButton_Click(object sender, EventArgs e)
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
        }

        // Show all settings
        private void showAllButton_Click(object sender, EventArgs e)
        {
            // Grab only the information available for the current board type
            String type = getBoardType();
            targetCB.SelectedItem = machine;
            switch (type)
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
                    fHitShowButton_Click(sender, e);
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
                    fHitShowButton_Click(sender, e);
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

        // Disable buttons not related to the board type
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
                    fHitShowButton.Enabled = false;
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
                    fHitShowButton.Enabled = false;
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
                    fHitShowButton.Enabled = false;
                    expSShowButton.Enabled = false;
                    break;
            }
        }

        // Enables all possibly disabled controls
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
            fHitShowButton.Enabled = true;
            expSShowButton.Enabled = true;
        }

        public void logData(String data) {
            //logTB.AppendText(data);
            data = data.Replace("\n", "");
            logTB.Text = "Hello";
        }

        private void form_closed(object sender, FormClosedEventArgs e)
        {
            conn.CloseConnection();
        }

        private void click_Click(object sender, EventArgs e)
        {
            Server tool = new Server(
                this,
                delegate(string message, int status)
                {
                    //this.logTB.Text = message;
                    this.logTB.AppendText(message);
                });
            tool.StartProcess(machine);
            //tool.StartConnection(machine);
        }

    }
}
