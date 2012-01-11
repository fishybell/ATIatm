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
            this.bGenerate = new System.Windows.Forms.Button();
            this.tbLogFilePath = new System.Windows.Forms.TextBox();
            this.label1 = new System.Windows.Forms.Label();
            this.crystalReportViewer1 = new CrystalDecisions.Windows.Forms.CrystalReportViewer();
            this.OpenFD = new System.Windows.Forms.OpenFileDialog();
            this.tbFileName = new System.Windows.Forms.TextBox();
            this.bReportFilePath = new System.Windows.Forms.Button();
            this.bLogFilePath = new System.Windows.Forms.Button();
            this.label4 = new System.Windows.Forms.Label();
            this.OpenFD2 = new System.Windows.Forms.OpenFileDialog();
            this.testButton = new System.Windows.Forms.Button();
            this.idTB = new System.Windows.Forms.TextBox();
            this.label2 = new System.Windows.Forms.Label();
            this.SuspendLayout();
            // 
            // bGenerate
            // 
            this.bGenerate.Location = new System.Drawing.Point(20, 148);
            this.bGenerate.Margin = new System.Windows.Forms.Padding(4, 4, 4, 4);
            this.bGenerate.Name = "bGenerate";
            this.bGenerate.Size = new System.Drawing.Size(148, 28);
            this.bGenerate.TabIndex = 0;
            this.bGenerate.Text = "Generate Report";
            this.bGenerate.UseVisualStyleBackColor = true;
            this.bGenerate.Click += new System.EventHandler(this.button1_Click);
            // 
            // tbLogFilePath
            // 
            this.tbLogFilePath.Location = new System.Drawing.Point(23, 31);
            this.tbLogFilePath.Margin = new System.Windows.Forms.Padding(4, 4, 4, 4);
            this.tbLogFilePath.Name = "tbLogFilePath";
            this.tbLogFilePath.Size = new System.Drawing.Size(235, 22);
            this.tbLogFilePath.TabIndex = 1;
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(16, 11);
            this.label1.Margin = new System.Windows.Forms.Padding(4, 0, 4, 0);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(91, 17);
            this.label1.TabIndex = 2;
            this.label1.Text = "Log File Path";
            // 
            // crystalReportViewer1
            // 
            this.crystalReportViewer1.ActiveViewIndex = -1;
            this.crystalReportViewer1.AutoSize = true;
            this.crystalReportViewer1.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.crystalReportViewer1.Cursor = System.Windows.Forms.Cursors.Default;
            this.crystalReportViewer1.Location = new System.Drawing.Point(23, 198);
            this.crystalReportViewer1.Margin = new System.Windows.Forms.Padding(4, 4, 4, 4);
            this.crystalReportViewer1.Name = "crystalReportViewer1";
            this.crystalReportViewer1.Size = new System.Drawing.Size(1366, 640);
            this.crystalReportViewer1.TabIndex = 5;
            this.crystalReportViewer1.ToolPanelWidth = 267;
            // 
            // OpenFD
            // 
            this.OpenFD.FileName = "OpenFileName";
            // 
            // tbFileName
            // 
            this.tbFileName.Location = new System.Drawing.Point(23, 96);
            this.tbFileName.Margin = new System.Windows.Forms.Padding(4, 4, 4, 4);
            this.tbFileName.Name = "tbFileName";
            this.tbFileName.Size = new System.Drawing.Size(235, 22);
            this.tbFileName.TabIndex = 8;
            // 
            // bReportFilePath
            // 
            this.bReportFilePath.Location = new System.Drawing.Point(267, 94);
            this.bReportFilePath.Margin = new System.Windows.Forms.Padding(4, 4, 4, 4);
            this.bReportFilePath.Name = "bReportFilePath";
            this.bReportFilePath.Size = new System.Drawing.Size(37, 28);
            this.bReportFilePath.TabIndex = 9;
            this.bReportFilePath.Text = "<<";
            this.bReportFilePath.UseVisualStyleBackColor = true;
            this.bReportFilePath.Click += new System.EventHandler(this.button2_Click);
            // 
            // bLogFilePath
            // 
            this.bLogFilePath.Location = new System.Drawing.Point(267, 31);
            this.bLogFilePath.Margin = new System.Windows.Forms.Padding(4, 4, 4, 4);
            this.bLogFilePath.Name = "bLogFilePath";
            this.bLogFilePath.Size = new System.Drawing.Size(37, 28);
            this.bLogFilePath.TabIndex = 10;
            this.bLogFilePath.Text = "<<";
            this.bLogFilePath.UseVisualStyleBackColor = true;
            this.bLogFilePath.Click += new System.EventHandler(this.bLogFilePath_Click);
            // 
            // label4
            // 
            this.label4.AutoSize = true;
            this.label4.Location = new System.Drawing.Point(16, 75);
            this.label4.Margin = new System.Windows.Forms.Padding(4, 0, 4, 0);
            this.label4.Name = "label4";
            this.label4.Size = new System.Drawing.Size(110, 17);
            this.label4.TabIndex = 11;
            this.label4.Text = "Report File Path";
            // 
            // OpenFD2
            // 
            this.OpenFD2.FileName = "OpenFD2";
            // 
            // testButton
            // 
            this.testButton.Location = new System.Drawing.Point(1303, 30);
            this.testButton.Margin = new System.Windows.Forms.Padding(4, 4, 4, 4);
            this.testButton.Name = "testButton";
            this.testButton.Size = new System.Drawing.Size(100, 28);
            this.testButton.TabIndex = 12;
            this.testButton.Text = "Test";
            this.testButton.UseVisualStyleBackColor = true;
            this.testButton.Click += new System.EventHandler(this.testButton_Click);
            // 
            // idTB
            // 
            this.idTB.Location = new System.Drawing.Point(1148, 30);
            this.idTB.Margin = new System.Windows.Forms.Padding(4, 4, 4, 4);
            this.idTB.Name = "idTB";
            this.idTB.Size = new System.Drawing.Size(132, 22);
            this.idTB.TabIndex = 13;
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(1148, 10);
            this.label2.Margin = new System.Windows.Forms.Padding(4, 0, 4, 0);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(75, 17);
            this.label2.TabIndex = 14;
            this.label2.Text = "ID Number";
            // 
            // MainForm
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(8F, 16F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(1432, 880);
            this.Controls.Add(this.label2);
            this.Controls.Add(this.idTB);
            this.Controls.Add(this.testButton);
            this.Controls.Add(this.label4);
            this.Controls.Add(this.bLogFilePath);
            this.Controls.Add(this.bReportFilePath);
            this.Controls.Add(this.tbFileName);
            this.Controls.Add(this.crystalReportViewer1);
            this.Controls.Add(this.label1);
            this.Controls.Add(this.tbLogFilePath);
            this.Controls.Add(this.bGenerate);
            this.Margin = new System.Windows.Forms.Padding(4, 4, 4, 4);
            this.Name = "MainForm";
            this.Text = "Form1";
            this.Load += new System.EventHandler(this.Form1_Load);
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Button bGenerate;
        private System.Windows.Forms.TextBox tbLogFilePath;
        private System.Windows.Forms.Label label1;
        private CrystalDecisions.Windows.Forms.CrystalReportViewer crystalReportViewer1;
        private System.Windows.Forms.OpenFileDialog OpenFD;
        private System.Windows.Forms.TextBox tbFileName;
        private System.Windows.Forms.Button bReportFilePath;
        private System.Windows.Forms.Button bLogFilePath;
        private System.Windows.Forms.Label label4;
        private System.Windows.Forms.OpenFileDialog OpenFD2;
        private System.Windows.Forms.Button testButton;
        private System.Windows.Forms.TextBox idTB;
        private System.Windows.Forms.Label label2;
    }
}

