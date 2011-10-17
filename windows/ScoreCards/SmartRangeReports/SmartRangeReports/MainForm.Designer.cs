namespace WindowsFormsApplication1
{
    partial class MainForm
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
            System.Console.WriteLine("1 Start of Initialize component");
            this.bGenerate = new System.Windows.Forms.Button();
            this.tbLogFilePath = new System.Windows.Forms.TextBox();
            this.label1 = new System.Windows.Forms.Label();
            this.label2 = new System.Windows.Forms.Label();
            this.textBox2 = new System.Windows.Forms.TextBox();
            this.crystalReportViewer1 = new CrystalDecisions.Windows.Forms.CrystalReportViewer();
            this.cbReportList = new System.Windows.Forms.ComboBox();
            this.label3 = new System.Windows.Forms.Label();
            this.OpenFD = new System.Windows.Forms.OpenFileDialog();
            this.tbFileName = new System.Windows.Forms.TextBox();
            this.bReportFilePath = new System.Windows.Forms.Button();
            this.bLogFilePath = new System.Windows.Forms.Button();
            this.label4 = new System.Windows.Forms.Label();
            this.OpenFD2 = new System.Windows.Forms.OpenFileDialog();
            this.SuspendLayout();
            // 
            // bGenerate
            // 
            System.Console.WriteLine("2 Start of Initialize component");
            this.bGenerate.Location = new System.Drawing.Point(15, 120);
            this.bGenerate.Name = "bGenerate";
            this.bGenerate.Size = new System.Drawing.Size(111, 23);
            this.bGenerate.TabIndex = 0;
            this.bGenerate.Text = "Generate Report";
            this.bGenerate.UseVisualStyleBackColor = true;
            this.bGenerate.Click += new System.EventHandler(this.button1_Click);
            // 
            // tbLogFilePath
            // 
            System.Console.WriteLine("3 Start of Initialize component");
            this.tbLogFilePath.Location = new System.Drawing.Point(17, 25);
            this.tbLogFilePath.Name = "tbLogFilePath";
            this.tbLogFilePath.Size = new System.Drawing.Size(177, 20);
            this.tbLogFilePath.TabIndex = 1;
            // 
            // label1
            // 
            System.Console.WriteLine("4 Start of Initialize component");
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(12, 9);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(69, 13);
            this.label1.TabIndex = 2;
            this.label1.Text = "Log File Path";
            // 
            // label2
            // 
            System.Console.WriteLine("5 Start of Initialize component");
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(530, 61);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(100, 13);
            this.label2.TabIndex = 3;
            this.label2.Text = "String From Log File";
            // 
            // textBox2
            // 
            System.Console.WriteLine("6 Start of Initialize component");
            this.textBox2.Location = new System.Drawing.Point(533, 78);
            this.textBox2.Name = "textBox2";
            this.textBox2.Size = new System.Drawing.Size(378, 20);
            this.textBox2.TabIndex = 4;
            // 
            // crystalReportViewer1
            // 
            System.Console.WriteLine("7 Start of Initialize component");
            this.crystalReportViewer1.ActiveViewIndex = -1;
            this.crystalReportViewer1.AutoSize = true;
            this.crystalReportViewer1.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.crystalReportViewer1.Cursor = System.Windows.Forms.Cursors.Default;
            this.crystalReportViewer1.Location = new System.Drawing.Point(17, 161);
            this.crystalReportViewer1.Name = "crystalReportViewer1";
            this.crystalReportViewer1.Size = new System.Drawing.Size(1045, 542);
            this.crystalReportViewer1.TabIndex = 5;
            // 
            // cbReportList
            // 
            System.Console.WriteLine("8 Start of Initialize component");
            this.cbReportList.FormattingEnabled = true;
            this.cbReportList.Items.AddRange(new object[] {
            "DA Form 3595R",
            "DA Form 3601R",
            "DA Form 5241R",
            "DA Form 7643R",
            "DA Form 7644R",
            "DA Form 7645R",
            "DA Form 7646R",
            "DA Form 7518R",
            "DA Form 7519R",
            "DA Form 7520R",
            "DA Form 7521R",
            "DA Form 7537R",
            "DA Form 7448R",
            "DA Form 7449R",
            "DA Form 7450R",
            "DA Form 7451R",
            "DA Form 85R",
            "DA Form 88R",
            "DA Form 7304R",
            "Malfunction",
            "Firing Order Summary"});
            this.cbReportList.Location = new System.Drawing.Point(271, 25);
            this.cbReportList.Name = "cbReportList";
            this.cbReportList.Size = new System.Drawing.Size(177, 21);
            this.cbReportList.TabIndex = 6;
            this.cbReportList.SelectedIndexChanged += new System.EventHandler(this.cbReportList_SelectedIndexChanged);
            // 
            // label3
            // 
            System.Console.WriteLine("9 Start of Initialize component");
            this.label3.AutoSize = true;
            this.label3.Location = new System.Drawing.Point(268, 9);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(78, 13);
            this.label3.TabIndex = 7;
            this.label3.Text = "Choose Report";
            // 
            // OpenFD
            // 
            System.Console.WriteLine("10 Start of Initialize component");
            this.OpenFD.FileName = "OpenFileName";
            // 
            // tbFileName
            // 
            System.Console.WriteLine("11 Start of Initialize component");
            this.tbFileName.Location = new System.Drawing.Point(17, 78);
            this.tbFileName.Name = "tbFileName";
            this.tbFileName.Size = new System.Drawing.Size(177, 20);
            this.tbFileName.TabIndex = 8;
            // 
            // bReportFilePath
            // 
            System.Console.WriteLine("12 Start of Initialize component");
            this.bReportFilePath.Location = new System.Drawing.Point(200, 76);
            this.bReportFilePath.Name = "bReportFilePath";
            this.bReportFilePath.Size = new System.Drawing.Size(28, 23);
            this.bReportFilePath.TabIndex = 9;
            this.bReportFilePath.Text = "<<";
            this.bReportFilePath.UseVisualStyleBackColor = true;
            this.bReportFilePath.Click += new System.EventHandler(this.button2_Click);
            // 
            // bLogFilePath
            // 
            System.Console.WriteLine("13 Start of Initialize component");
            this.bLogFilePath.Location = new System.Drawing.Point(200, 25);
            this.bLogFilePath.Name = "bLogFilePath";
            this.bLogFilePath.Size = new System.Drawing.Size(28, 23);
            this.bLogFilePath.TabIndex = 10;
            this.bLogFilePath.Text = "<<";
            this.bLogFilePath.UseVisualStyleBackColor = true;
            this.bLogFilePath.Click += new System.EventHandler(this.bLogFilePath_Click);
            // 
            // label4
            // 
            System.Console.WriteLine("14 Start of Initialize component");
            this.label4.AutoSize = true;
            this.label4.Location = new System.Drawing.Point(12, 61);
            this.label4.Name = "label4";
            this.label4.Size = new System.Drawing.Size(83, 13);
            this.label4.TabIndex = 11;
            this.label4.Text = "Report File Path";
            // 
            // OpenFD2
            // 
            System.Console.WriteLine("15 Start of Initialize component");
            this.OpenFD2.FileName = "OpenFD2";
            // 
            // MainForm
            // 
            System.Console.WriteLine("16 Start of Initialize component");
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(1074, 715);
            this.Controls.Add(this.label4);
            this.Controls.Add(this.bLogFilePath);
            this.Controls.Add(this.bReportFilePath);
            this.Controls.Add(this.tbFileName);
            this.Controls.Add(this.label3);
            this.Controls.Add(this.cbReportList);
            this.Controls.Add(this.crystalReportViewer1);
            this.Controls.Add(this.textBox2);
            this.Controls.Add(this.label2);
            this.Controls.Add(this.label1);
            this.Controls.Add(this.tbLogFilePath);
            this.Controls.Add(this.bGenerate);
            this.Name = "MainForm";
            this.Text = "Form1";
            this.Load += new System.EventHandler(this.Form1_Load);
            this.ResumeLayout(false);
            this.PerformLayout();
            System.Console.WriteLine("End of Initialize component");
        }

        #endregion

        private System.Windows.Forms.Button bGenerate;
        private System.Windows.Forms.TextBox tbLogFilePath;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.TextBox textBox2;
        private CrystalDecisions.Windows.Forms.CrystalReportViewer crystalReportViewer1;
        private System.Windows.Forms.ComboBox cbReportList;
        private System.Windows.Forms.Label label3;
        private System.Windows.Forms.OpenFileDialog OpenFD;
        private System.Windows.Forms.TextBox tbFileName;
        private System.Windows.Forms.Button bReportFilePath;
        private System.Windows.Forms.Button bLogFilePath;
        private System.Windows.Forms.Label label4;
        private System.Windows.Forms.OpenFileDialog OpenFD2;
    }
}

