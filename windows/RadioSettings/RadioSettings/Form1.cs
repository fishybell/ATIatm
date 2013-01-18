using System;
using System.Collections.Generic;
using System.Collections;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using System.IO;

namespace RadioSettings
{
    public partial class Form1 : Form
    {
        public Form1()
        {
            InitializeComponent();
            loadCurrentSettings();
        }

        /*********************************************
         * Load the Current settings from mcp.settings
         * ********************************************/
        private void loadCurrentSettings()
        {
            if (File.Exists(".\\mcp.settings") && !Variables.readExisting)
            {
                Variables.readExisting = true;
                TextReader tr = new StreamReader(".\\mcp.settings");
                //Variables.advancedList.Clear();

                int i = 0;
                string line = tr.ReadLine();
                while (line != null)
                {
                    int firstIndex = line.IndexOf('=') + 1;
                    int length = line.Length;
                    int colonLength = line.LastIndexOf(':');
                    int secondIndex = (colonLength <= length && colonLength != -1) ? colonLength : length;
                    string thisSetting = line.Substring(firstIndex, secondIndex - firstIndex);

                    // Connect port is a special case
                    if (i == 0)
                    {
                        string connectPort = line.Substring(colonLength + 1, length - colonLength - 1);
                        Variables.advancedList[16] = connectPort;
                    }
                    Variables.advancedList[i] = thisSetting;

                    i++;
                    line = tr.ReadLine();
                }
                tr.Close();
                Variables.assignVariables();
            }
            // Fill in textboxes
            rangeIP.Text = Variables.advancedList[0];
            baseIP.Text = Variables.advancedList[1];
            lowMAC.Text = Variables.advancedList[2];
            highMAC.Text = Variables.advancedList[3];
            cport.Text = Variables.advancedList[16];
           
        }

        /******************************************
         * Change Settings button is clicked
         * ****************************************/
        private void changeBTN_Click(object sender, EventArgs e)
        {
            if (string.IsNullOrWhiteSpace(rangeIP.Text) || string.IsNullOrWhiteSpace(baseIP.Text)
                || string.IsNullOrWhiteSpace(lowMAC.Text) || string.IsNullOrWhiteSpace(highMAC.Text)
                || string.IsNullOrWhiteSpace(cport.Text))
            {
                errorLBL.Visible = true;
            }
            else
            {
                // Grab the textbox values
                Variables.computer = rangeIP.Text;
                Variables.basestation = baseIP.Text;
                Variables.lowmac = lowMAC.Text;
                Variables.highmac = highMAC.Text;
                Variables.port = cport.Text;

                writeFile(Variables.computer, Variables.basestation, Variables.lowmac, Variables.highmac, Variables.port);

                //TODO - Copy mcp.settings over to the linux box
                //TODO - Stop MCP
                //TODO - Start MCP

                // Destroy Forms
                this.Close();
                using (var form2 = new Form2())
                {
                    form2.Close();
                }
            }
        }

        /****************************************************
         * Write the settings to a file called mcp.settings 
         * **************************************************/
        private void writeFile(string computer, string basestation, string lowmac, string highmac, string port)
        {
            string[] lines = {
                               "fasit=" + computer + ":" + port,
                               "rfmaster=" + basestation + ":4004",
                               "lowdev=" + lowmac,
                               "highdev=" + highmac,
                               "verbose=" + Variables.verbose,
                               "slottime=" + Variables.slottime,
                               "inittime=" + Variables.inittime,
                               "hunttime=" + Variables.hunttime,
                               "slowtime=" + Variables.slowtime,
                               "fasttime=" + Variables.fasttime,
                               "mit_length=" + Variables.mit_length,
                               "mat_length=" +  Variables.mat_length,
                               "mit_home=" + Variables.mit_home,
                               "mat_home=" + Variables.mat_home,
                               "mit_end=" + Variables.mit_end,
                               "mat_end=" + Variables.mat_end
                           };

            File.WriteAllLines(".\\mcp.settings", lines);
        }

        private void advancedBTN_Click(object sender, EventArgs e)
        {
            // Save the text values
            // Grab the textbox values
            Variables.computer = rangeIP.Text;
            Variables.basestation = baseIP.Text;
            Variables.lowmac = lowMAC.Text;
            Variables.highmac = highMAC.Text;
            Variables.port = cport.Text;
            Variables.reverseAssign1();

            this.Hide();
            using (var form2 = new Form2())
            {
                form2.ShowDialog();
            }
        }

    }
}
