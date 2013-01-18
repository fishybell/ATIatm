namespace RadioSettings
{
    partial class Form1
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.components = new System.ComponentModel.Container();
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(Form1));
            this.label1 = new System.Windows.Forms.Label();
            this.label2 = new System.Windows.Forms.Label();
            this.rangeIP = new System.Windows.Forms.TextBox();
            this.baseIP = new System.Windows.Forms.TextBox();
            this.label3 = new System.Windows.Forms.Label();
            this.lowMAC = new System.Windows.Forms.TextBox();
            this.label4 = new System.Windows.Forms.Label();
            this.highMAC = new System.Windows.Forms.TextBox();
            this.label6 = new System.Windows.Forms.Label();
            this.changeBTN = new System.Windows.Forms.Button();
            this.pictureBox1 = new System.Windows.Forms.PictureBox();
            this.toolTip1 = new System.Windows.Forms.ToolTip(this.components);
            this.advancedBTN = new System.Windows.Forms.Button();
            this.label8 = new System.Windows.Forms.Label();
            this.cport = new System.Windows.Forms.TextBox();
            this.label9 = new System.Windows.Forms.Label();
            this.errorLBL = new System.Windows.Forms.Label();
            ((System.ComponentModel.ISupportInitialize)(this.pictureBox1)).BeginInit();
            this.SuspendLayout();
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Font = new System.Drawing.Font("Microsoft Sans Serif", 9F, ((System.Drawing.FontStyle)(((System.Drawing.FontStyle.Bold | System.Drawing.FontStyle.Italic) 
                | System.Drawing.FontStyle.Underline))), System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label1.Location = new System.Drawing.Point(28, 31);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(542, 18);
            this.label1.TabIndex = 0;
            this.label1.Text = "To connect radio targets to your range please fill in the following fields.";
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(32, 70);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(203, 17);
            this.label2.TabIndex = 1;
            this.label2.Text = "IP Address of Range Computer";
            // 
            // rangeIP
            // 
            this.rangeIP.Location = new System.Drawing.Point(32, 91);
            this.rangeIP.Name = "rangeIP";
            this.rangeIP.Size = new System.Drawing.Size(100, 22);
            this.rangeIP.TabIndex = 2;
            // 
            // baseIP
            // 
            this.baseIP.Location = new System.Drawing.Point(32, 155);
            this.baseIP.Name = "baseIP";
            this.baseIP.Size = new System.Drawing.Size(100, 22);
            this.baseIP.TabIndex = 4;
            // 
            // label3
            // 
            this.label3.AutoSize = true;
            this.label3.Location = new System.Drawing.Point(32, 134);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(170, 17);
            this.label3.TabIndex = 3;
            this.label3.Text = "IP Address of Basestation";
            // 
            // lowMAC
            // 
            this.lowMAC.Location = new System.Drawing.Point(32, 221);
            this.lowMAC.Name = "lowMAC";
            this.lowMAC.Size = new System.Drawing.Size(100, 22);
            this.lowMAC.TabIndex = 6;
            this.toolTip1.SetToolTip(this.lowMAC, "Example: 70:5E:AA:00:01:BF, Enter: 0x1BF");
            // 
            // label4
            // 
            this.label4.AutoSize = true;
            this.label4.Location = new System.Drawing.Point(32, 200);
            this.label4.Name = "label4";
            this.label4.Size = new System.Drawing.Size(417, 17);
            this.label4.TabIndex = 5;
            this.label4.Text = "Lowest Target MAC Address to Connect - (0x<last 3 characters>)";
            // 
            // highMAC
            // 
            this.highMAC.Location = new System.Drawing.Point(33, 287);
            this.highMAC.Name = "highMAC";
            this.highMAC.Size = new System.Drawing.Size(100, 22);
            this.highMAC.TabIndex = 9;
            this.toolTip1.SetToolTip(this.highMAC, "Example: 70:5E:AA:00:01:BF, Enter: 0x1BF");
            // 
            // label6
            // 
            this.label6.AutoSize = true;
            this.label6.Location = new System.Drawing.Point(33, 266);
            this.label6.Name = "label6";
            this.label6.Size = new System.Drawing.Size(421, 17);
            this.label6.TabIndex = 8;
            this.label6.Text = "Highest Target MAC Address to Connect - (0x<last 3 characters>)";
            // 
            // changeBTN
            // 
            this.changeBTN.Location = new System.Drawing.Point(412, 354);
            this.changeBTN.Name = "changeBTN";
            this.changeBTN.Size = new System.Drawing.Size(151, 34);
            this.changeBTN.TabIndex = 14;
            this.changeBTN.Text = "Change Settings";
            this.changeBTN.UseVisualStyleBackColor = true;
            this.changeBTN.Click += new System.EventHandler(this.changeBTN_Click);
            // 
            // pictureBox1
            // 
            this.pictureBox1.Image = ((System.Drawing.Image)(resources.GetObject("pictureBox1.Image")));
            this.pictureBox1.Location = new System.Drawing.Point(389, 79);
            this.pictureBox1.Name = "pictureBox1";
            this.pictureBox1.Size = new System.Drawing.Size(147, 50);
            this.pictureBox1.TabIndex = 15;
            this.pictureBox1.TabStop = false;
            // 
            // advancedBTN
            // 
            this.advancedBTN.Location = new System.Drawing.Point(322, 354);
            this.advancedBTN.Name = "advancedBTN";
            this.advancedBTN.Size = new System.Drawing.Size(84, 34);
            this.advancedBTN.TabIndex = 16;
            this.advancedBTN.Text = "Advanced";
            this.advancedBTN.UseVisualStyleBackColor = true;
            this.advancedBTN.Click += new System.EventHandler(this.advancedBTN_Click);
            // 
            // label8
            // 
            this.label8.AutoSize = true;
            this.label8.Location = new System.Drawing.Point(33, 332);
            this.label8.Name = "label8";
            this.label8.Size = new System.Drawing.Size(109, 17);
            this.label8.TabIndex = 11;
            this.label8.Text = "Connection Port";
            // 
            // cport
            // 
            this.cport.Location = new System.Drawing.Point(33, 353);
            this.cport.Name = "cport";
            this.cport.Size = new System.Drawing.Size(100, 22);
            this.cport.TabIndex = 12;
            // 
            // label9
            // 
            this.label9.AutoSize = true;
            this.label9.Location = new System.Drawing.Point(142, 354);
            this.label9.Name = "label9";
            this.label9.Size = new System.Drawing.Size(93, 17);
            this.label9.TabIndex = 13;
            this.label9.Text = "Default: 4000";
            // 
            // errorLBL
            // 
            this.errorLBL.AutoSize = true;
            this.errorLBL.Font = new System.Drawing.Font("Microsoft Sans Serif", 7.8F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.errorLBL.ForeColor = System.Drawing.Color.DarkRed;
            this.errorLBL.Location = new System.Drawing.Point(347, 321);
            this.errorLBL.Name = "errorLBL";
            this.errorLBL.Size = new System.Drawing.Size(189, 17);
            this.errorLBL.TabIndex = 17;
            this.errorLBL.Text = "You must fill in all boxes.";
            this.errorLBL.Visible = false;
            // 
            // Form1
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(8F, 16F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.BackColor = System.Drawing.Color.DarkSeaGreen;
            this.ClientSize = new System.Drawing.Size(595, 400);
            this.Controls.Add(this.errorLBL);
            this.Controls.Add(this.advancedBTN);
            this.Controls.Add(this.pictureBox1);
            this.Controls.Add(this.changeBTN);
            this.Controls.Add(this.label9);
            this.Controls.Add(this.cport);
            this.Controls.Add(this.label8);
            this.Controls.Add(this.highMAC);
            this.Controls.Add(this.label6);
            this.Controls.Add(this.lowMAC);
            this.Controls.Add(this.label4);
            this.Controls.Add(this.baseIP);
            this.Controls.Add(this.label3);
            this.Controls.Add(this.rangeIP);
            this.Controls.Add(this.label2);
            this.Controls.Add(this.label1);
            this.Name = "Form1";
            this.Text = "Radio Settings";
            ((System.ComponentModel.ISupportInitialize)(this.pictureBox1)).EndInit();
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.TextBox rangeIP;
        private System.Windows.Forms.TextBox baseIP;
        private System.Windows.Forms.Label label3;
        private System.Windows.Forms.TextBox lowMAC;
        private System.Windows.Forms.Label label4;
        private System.Windows.Forms.TextBox highMAC;
        private System.Windows.Forms.Label label6;
        private System.Windows.Forms.Button changeBTN;
        private System.Windows.Forms.PictureBox pictureBox1;
        private System.Windows.Forms.ToolTip toolTip1;
        private System.Windows.Forms.Button advancedBTN;
        private System.Windows.Forms.Label label8;
        private System.Windows.Forms.TextBox cport;
        private System.Windows.Forms.Label label9;
        private System.Windows.Forms.Label errorLBL;
    }
}

