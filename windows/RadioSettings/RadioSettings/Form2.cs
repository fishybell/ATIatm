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
    public partial class Form2 : Form
    {
        public static List<string> advancedList2 = new List<string>();

        public Form2()
        {
            InitializeComponent();
            loadCurrentAdvancedSettings();
        }

        private void loadCurrentAdvancedSettings()
        {
            if (File.Exists(".\\mcp.settings"))
            {
                Control[] controlText = { verboseTB, slotTB, initTB, huntTB, slowTB, fastTB, mitLengthTB, 
                                            matLengthTB, mitHomeTB, matHomeTB, mitEndTB, matEndTB };
                int i = 4;
                foreach (var item in controlText)
                {
                    item.Text = Variables.advancedList[i];
                    i++;
                }

            }
        }   

        private void backBTN_Click(object sender, EventArgs e)
        {
            // Get current Form2 textbox values
            Control[] controlText = { verboseTB, slotTB, initTB, huntTB, slowTB, fastTB, mitLengthTB, 
                                            matLengthTB, mitHomeTB, matHomeTB, mitEndTB, matEndTB };

            int i = 4;
            foreach (var item in controlText)
            {
                Variables.advancedList[i] = item.Text;
                i++;
            }
            Variables.assignVariables();
            this.Hide();
            using (var form1 = new Form1())
            {
                form1.ShowDialog();
            }
        }



        private void changeBTN_Click(object sender, EventArgs e)
        {
            if (string.IsNullOrWhiteSpace(Variables.computer) || string.IsNullOrWhiteSpace(Variables.basestation)
                || string.IsNullOrWhiteSpace(Variables.lowmac) || string.IsNullOrWhiteSpace(Variables.highmac)
                || string.IsNullOrWhiteSpace(Variables.port) || string.IsNullOrWhiteSpace(slotTB.Text)
                || string.IsNullOrWhiteSpace(initTB.Text) || string.IsNullOrWhiteSpace(huntTB.Text)
                || string.IsNullOrWhiteSpace(slowTB.Text) || string.IsNullOrWhiteSpace(fastTB.Text)
                || string.IsNullOrWhiteSpace(mitLengthTB.Text) || string.IsNullOrWhiteSpace(matLengthTB.Text)
                || string.IsNullOrWhiteSpace(mitHomeTB.Text) || string.IsNullOrWhiteSpace(matHomeTB.Text)
                || string.IsNullOrWhiteSpace(mitEndTB.Text) || string.IsNullOrWhiteSpace(matEndTB.Text)
                || string.IsNullOrWhiteSpace(verboseTB.Text))
            {
                errorLBL.Visible = true;
                error2LBL.Visible = true;
            }
            else
            {
                // Grab the textbox values
                string slot = slotTB.Text;
                string init = initTB.Text;
                string hunt = huntTB.Text;
                string slow = slowTB.Text;
                string fast = fastTB.Text;
                string mitlength = mitLengthTB.Text;
                string matlength = matLengthTB.Text;
                string mithome = mitHomeTB.Text;
                string mathome = matHomeTB.Text;
                string mitend = mitEndTB.Text;
                string matend = matEndTB.Text;
                string verbose = verboseTB.Text;

                writeFile(Variables.computer, Variables.basestation, Variables.lowmac, Variables.highmac, Variables.port,
                    slot, init, hunt, slow, fast, mitlength, matlength, mithome, mathome, mitend, matend, verbose);

                // Destroy Forms
                this.Close();
                using (var form1 = new Form2())
                {
                    form1.Close();
                }
            }
            
            
        }

        private void writeFile(string computer, string basestation, string lowmac, string highmac, string port, string slot, string init, 
            string hunt, string slow, string fast, string mitlength, string matlength, string mithome, string mathome, 
            string mitend, string matend, string verbose)
        {
            string[] lines = {
                               "fasit=" + computer + ":" + port,
                               "rfmaster=" + basestation + ":4004",
                               "lowdev=" + lowmac,
                               "highdev=" + highmac,
                               "verbose=" + verbose,
                               "slottime=" + slot,
                               "inittime=" + init,
                               "hunttime=" + hunt,
                               "slowtime=" + slow,
                               "fasttime=" + fast,
                               "mit_length=" + mitlength,
                               "mat_length=" +  matlength,
                               "mit_home=" + mithome,
                               "mat_home=" + mathome,
                               "mit_end=" + mitend,
                               "mat_end=" + matend
                           };

            File.WriteAllLines(".\\mcp.settings", lines);
        }
    }
}
