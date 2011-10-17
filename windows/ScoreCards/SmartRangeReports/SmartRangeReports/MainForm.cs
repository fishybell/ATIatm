using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using System.IO;
using System.Collections;
using CrystalDecisions.CrystalReports.Engine;
using CrystalDecisions.Shared;




namespace WindowsFormsApplication1
{
    public partial class MainForm : Form
    {
        public MainForm()
        {
            InitializeComponent();
        }

        private void button1_Click(object sender, EventArgs e)
        {
            string sPath, sFile;
            string sText, sLineText, sTemp, sTable, sTask, sTime, sRound, sRange;
            int iPos, iPos2, iLen, iElement;
            int iStartPos;
            string sDate = "", sIDCode, sLane, exp, sName, sSelected;
            bool bOut;
            //string[] sIDCodes;

            //textBox1.Text = "c:\\dummy.txt";
            sPath = tbLogFilePath.Text;

            StreamReader streamReader = new StreamReader(sPath);
            sText = streamReader.ReadToEnd();
            streamReader.Close();

            textBox2.Text = sText;

            //string[] sIDCodes = new string[];
            string[] sIDCodes = new string[21];
            iElement = -1;  //used for the array

            DataSet1 ds = new DataSet1();
            DataTable dt1 = ds.ID;
            DataTable dt2 = ds.HIT;
            DataTable dt3 = ds.TASKSTART;
            DataTable dt4 = ds.TASKEND;
            DataTable dt5 = ds.TARGET;
            DataTable dt6 = ds.SPOTTER;
            DataTable dt7 = ds.MALFUNCTION;
            DataTable dt8 = ds.SCENARIOSTART;
            DataTable dt9 = ds.TARGETEXPOSED;
            DataTable dt10 = ds.TARGETCONCEALED;

            DataRow r;
            DataRow r2;
            DataRow r3;
            DataRow r4;
            DataRow r5;
            DataRow r6;
            DataRow r7;
            DataRow r8;
            DataRow r9;
            DataRow r10;

            DataRow[] foundRows;

            
            bOut = false;
            while (bOut == false)    //bOut is set to true when the log file parse is complete
            {
                iStartPos = sText.Length;

                // Find the next line header
                
                iStartPos = GetStartPos(sText, "SCENARIOSTART:", iStartPos);

                iStartPos = GetStartPos(sText, "SHOOTER:", iStartPos);

                iStartPos = GetStartPos(sText, "SPOTTER:", iStartPos);

                iStartPos = GetStartPos(sText, "TASKSTART:", iStartPos);

                iStartPos = GetStartPos(sText, "TASKEND:", iStartPos);

                iStartPos = GetStartPos(sText, "HIT:", iStartPos);

                iStartPos = GetStartPos(sText, "TARGET:", iStartPos);

                iStartPos = GetStartPos(sText, "TARGETEXPOSED:", iStartPos);

                iStartPos = GetStartPos(sText, "TARGETCONCEALED:", iStartPos);

                iStartPos = GetStartPos(sText, "TASKABORT:", iStartPos);

                iStartPos = GetStartPos(sText, "SCENARIOEND:", iStartPos);

                iStartPos = GetStartPos(sText, "MALFUNCTION:", iStartPos);

                // Take off all characters before the header
                iLen = sText.Length - iStartPos;
                if (iLen > 0)
                    sText = Mid(sText, iStartPos, iLen);

                // Find the end of the line (signified by ';')
                iPos = sText.IndexOf(";", 0);

                // At the end of the file so return a blank string
                if (iPos < 0)
                {
                    iPos = 0;
                }
                sLineText = sText.Substring(0, iPos);
                iPos += 1;
                iLen = sText.Length - iPos;
                if (iLen > 0)
                    sText = Mid(sText, iPos, iLen);
                else
                    bOut = true;  // This means that we are at the end, so exit the while loop

                //Get the header from the line (sLineText)
                iPos = sLineText.IndexOf(":", 0);
                if (iPos < 0)
                {
                    bOut = true;
                    sTemp = "temp";
                }
                else
                {
                    sTemp = sLineText.Substring(0, iPos);
                    sTemp = sTemp.ToUpper();
                }
                switch (sTemp)
                {
                    case "SCENARIOSTART":

                        r8 = dt8.NewRow();

                        iPos = sLineText.IndexOf("TIMESTAMP(", 0);
                        iPos += 10;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sDate = sLineText.Substring(iPos, iLen);
                        r8["TIMESTAMP"] = sDate;

                        iPos = sLineText.IndexOf("NAME(", 0);
                        iPos += 5;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r8["NAME"] = sName;

                        dt8.Rows.Add(r8);
                        break;
                    case "SHOOTER":


                        r = dt1.NewRow();

                        iPos = sLineText.IndexOf("IDCODE(", 0);
                        iPos += 7;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sIDCode = sLineText.Substring(iPos, iLen);
                        r["IDCode"] = sIDCode;
                        iElement++;
                        sIDCodes[iElement] = sIDCode;   // store this for opening multiple reports

                        iPos = sLineText.IndexOf("UNIT(", 0);
                        iPos += 5;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sLane = sLineText.Substring(iPos, iLen);
                        r["UNIT"] = sLane;

                        iPos = sLineText.IndexOf("GROUP(", 0);
                        iPos += 6;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sLane = sLineText.Substring(iPos, iLen);
                        r["GROUP"] = sLane;

                        iPos = sLineText.IndexOf("LASTNAME(", 0);
                        iPos += 9;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r["LASTNAME"] = sName;

                        iPos = sLineText.IndexOf("FIRSTNAME(", 0);
                        iPos += 10;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r["FIRSTNAME"] = sName;

                        iPos = sLineText.IndexOf("MIDDLE(", 0);
                        iPos += 7;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r["MIDDLE"] = sName;

                        iPos = sLineText.IndexOf("RANK(", 0);
                        iPos += 5;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sLane = sLineText.Substring(iPos, iLen);
                        r["RANK"] = sLane;

                        r["DATE"] = sDate;

                        dt1.Rows.Add(r);
                        break;

                    case "SPOTTER":

                        r6 = dt6.NewRow();

                        iPos = sLineText.IndexOf("LASTNAME(", 0);
                        iPos += 9;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r6["LASTNAME"] = sName;

                        iPos = sLineText.IndexOf("FIRSTNAME(", 0);
                        iPos += 10;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r6["FIRSTNAME"] = sName;

                        iPos = sLineText.IndexOf("GROUP(", 0);
                        iPos += 6;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sLane = sLineText.Substring(iPos, iLen);
                        r6["GROUP"] = sLane;

                        dt6.Rows.Add(r6);

                        break;


                    case "TASKSTART":  //TARGET(TABLE 2 TASK 2 QUALIFY), NAME(abc2), TIMESTAMP(2011/04/21 12:04:20);
                        r3 = dt3.NewRow();

                        iPos = sLineText.IndexOf("TABLE(", 0);
                        iPos += 6;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sTable = sLineText.Substring(iPos, iLen);
                        r3["TABLE"] = sTable;

                        iPos = sLineText.IndexOf("TASK(", 0);
                        iPos += 5;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sTask = sLineText.Substring(iPos, iLen);
                        r3["TASK"] = sTask;

                        iPos = sLineText.IndexOf("RANGE(", 0);
                        iPos += 6;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sRange = sLineText.Substring(iPos, iLen);
                        r3["RANGE"] = sRange;

                        iPos = sLineText.IndexOf("ROUND(", 0);
                        iPos += 6;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sRound = sLineText.Substring(iPos, iLen);
                        r3["ROUND"] = sRound;

                        iPos = sLineText.IndexOf("TIMESTAMP", 0);
                        iPos += 10;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sTime = sLineText.Substring(iPos, iLen);
                        r3["TIMESTAMP"] = sTime;

                        //Make sure that there are no duplicate rows already in the datatable
                        //  If there are, then they must be deleted from the table.
                        exp = "TABLE = '" + sTable + "' AND TASK = '" + sTask + "' AND RANGE = '" + sRange + "' AND ROUND = '" + sRound + "'";
                        foundRows = dt3.Select(exp);
                        //int i = foundRows[0].ToString;
                        if (foundRows.Length > 0)
                            foundRows[0].Delete();

                        dt3.Rows.Add(r3);
                        break;

                    case "HIT":   // HIT: NAME(abc4), TIMESTAMP(2011/04/21 12:10:12);
                        r2 = dt2.NewRow();

                        iPos = sLineText.IndexOf("NAME(", 0);
                        iPos += 5;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r2["NAME"] = sName;

                        iPos = sLineText.IndexOf("TIMESTAMP(", 0);
                        iPos += 10;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sTime = sLineText.Substring(iPos, iLen);
                        r2["TIMESTAMP"] = sTime;


                        dt2.Rows.Add(r2);

                        break;

                    case "TASKEND":    
                        r4 = dt4.NewRow();

                        iPos = sLineText.IndexOf("TABLE(", 0);
                        iPos += 6;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sTable = sLineText.Substring(iPos, iLen);
                        r4["TABLE"] = sTable;

                        iPos = sLineText.IndexOf("TASK(", 0);
                        iPos += 5;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sTask = sLineText.Substring(iPos, iLen);
                        r4["TASK"] = sTask;

                        iPos = sLineText.IndexOf("RANGE(", 0);
                        iPos += 6;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sRange = sLineText.Substring(iPos, iLen);
                        r4["RANGE"] = sRange;

                        iPos = sLineText.IndexOf("ROUND(", 0);
                        iPos += 6;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sRound = sLineText.Substring(iPos, iLen);
                        r4["ROUND"] = sRound;

                        iPos = sLineText.IndexOf("TIMESTAMP", 0);
                        iPos += 10;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sTime = sLineText.Substring(iPos, iLen);
                        r4["TIMESTAMP"] = sTime;

                        //Make sure that there are no duplicate rows already in the datatable
                        //  If there are, then they must be deleted from the table.
                        exp = "TABLE = '" + sTable + "' AND TASK = '" + sTask + "' AND RANGE = '" + sRange + "' AND ROUND = '" + sRound + "'";
                        foundRows = dt4.Select(exp);
                        //int i = foundRows[0].ToString;
                        if (foundRows.Length > 0)
                            foundRows[0].Delete();

                        dt4.Rows.Add(r4);
                        break;


                    case "TARGET":
                        r5 = dt5.NewRow();

                        iPos = sLineText.IndexOf("RANGE(", 0);
                        iPos += 6;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sRange = sLineText.Substring(iPos, iLen);
                        r5["RANGE"] = sRange;

                        iPos = sLineText.IndexOf("NAME(", 0);
                        iPos += 5;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r5["NAME"] = sName;

                        iPos = sLineText.IndexOf("GROUP(", 0);
                        iPos += 6;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sLane = sLineText.Substring(iPos, iLen);
                        r5["GROUP"] = sLane;


                        dt5.Rows.Add(r5);
                        break;


                    case "TARGETEXPOSED":
                        r9 = dt9.NewRow();

                        iPos = sLineText.IndexOf("NAME(", 0);
                        iPos += 5;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r9["NAME"] = sName;

                        iPos = sLineText.IndexOf("TIMESTAMP(", 0);
                        iPos += 10;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sTime = sLineText.Substring(iPos, iLen);
                        r9["TIMESTAMP"] = sTime;

                        iPos = sLineText.IndexOf("ATTRITION(", 0);
                        iPos += 10;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sLane = sLineText.Substring(iPos, iLen);
                        r9["ATTRITION"] = sLane;


                        dt9.Rows.Add(r9);
                        break;

                    case "TARGETCONCEALED":
                        r10 = dt10.NewRow();

                        iPos = sLineText.IndexOf("NAME(", 0);
                        iPos += 5;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r10["NAME"] = sName;

                        iPos = sLineText.IndexOf("TIMESTAMP(", 0);
                        iPos += 10;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sTime = sLineText.Substring(iPos, iLen);
                        r10["TIMESTAMP"] = sTime;

                        iPos = sLineText.IndexOf("KILL(", 0);
                        iPos += 5;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sLane = sLineText.Substring(iPos, iLen);
                        r10["KILL"] = sLane;


                        dt10.Rows.Add(r10);
                        break;

                    case "MALFUNCTION":

                        r7 = dt7.NewRow();

                        iPos = sLineText.IndexOf("ADDRESS(", 0);
                        iPos += 8;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r7["ADDRESS"] = sName;

                        iPos = sLineText.IndexOf("TIMESTAMP(", 0);
                        iPos += 10;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sName = sLineText.Substring(iPos, iLen);
                        r7["TIMESTAMP"] = sName;

                        iPos = sLineText.IndexOf("DEFINITION(", 0);
                        iPos += 11;
                        iPos2 = sLineText.IndexOf(")", iPos);
                        iLen = iPos2 - iPos;
                        sLane = sLineText.Substring(iPos, iLen);
                        r7["DEFINITION"] = sLane;

                        dt7.Rows.Add(r7);

                        break;

                    case "SCENARIOEND":
                        break;



                }




            }

            
            //for (i = 0; i <= iElement; i++)
            //{
            //CrystalReport3 cryRpt = new CrystalReport3();
            //Report_DA_Form_3595R cryRpt0 = new Report_DA_Form_3595R();
            //Report_DA_Form_3601R cryRpt1 = new Report_DA_Form_3601R();
            //Report_DA_Form_5241R cryRpt2 = new Report_DA_Form_5241R();
            //Report_DA_Form_7643R cryRpt3 = new Report_DA_Form_7643R();
            //Report_DA_Form_7644R cryRpt4 = new Report_DA_Form_7644R();
            //Report_DA_Form_7645R cryRpt5 = new Report_DA_Form_7645R();
            //Report_DA_Form_7646R cryRpt6 = new Report_DA_Form_7646R();
            //Report_DA_Form_7448R cryRpt7 = new Report_DA_Form_7448R();
            //Report_DA_Form_7449R cryRpt8 = new Report_DA_Form_7449R();
            //Report_DA_Form_7537R cryRpt9 = new Report_DA_Form_7537R();
            //Report_DA_Form_7521R cryRpt10 = new Report_DA_Form_7521R();
            //Report_DA_Form_7520R cryRpt11 = new Report_DA_Form_7520R();
            //Report_DA_Form_7519R cryRpt12 = new Report_DA_Form_7519R();
            //Report_DA_Form_7518R cryRpt13 = new Report_DA_Form_7518R();
            //Report_DA_Form_7450R cryRpt14 = new Report_DA_Form_7450R();
            //Report_DA_Form_7451R cryRpt15 = new Report_DA_Form_7451R();
            //Report_DA_Form_88R cryRpt16 = new Report_DA_Form_88R();
            //Report_DA_Form_85R cryRpt17 = new Report_DA_Form_85R();
            //Report_DA_Form_7304R cryRpt18 = new Report_DA_Form_7304R();
            //Report_MALFUNCTION cryRpt19 = new Report_MALFUNCTION();
            //Report_Firing_Order_Summary cryRpt20 = new Report_Firing_Order_Summary();

            //sSelected = cbReportList.SelectedItem.ToString();
            //switch (sSelected)
            //{
            //    case "DA Form 3595R":
            //        cryRpt0.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt0;
            //        break;

            //    case "DA Form 3601R":
            //        cryRpt1.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt1;
            //        break;

            //    case "DA Form 5241R":
            //        cryRpt2.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt2;
            //        break;

            //    case "DA Form 7643R":
            //        cryRpt3.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt3;
            //        break;

            //    case "DA Form 7644R":
            //        cryRpt4.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt4;
            //        break;

            //    case "DA Form 7645R":
            //        cryRpt5.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt5;
            //        break;

            //    case "DA Form 7646R":
            //        cryRpt6.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt6;
            //        break;

            //    case "DA Form 7518R":
            //        cryRpt7.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt7;
            //        break;

            //    case "DA Form 7519R":
            //        cryRpt8.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt8;
            //        break;

            //    case "DA Form 7520R":
            //        cryRpt9.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt9;
            //        break;

            //    case "DA Form 7521R":
            //        cryRpt10.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt10;
            //        break;

            //    case "DA Form 7537R":
            //        cryRpt11.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt11;
            //        break;

            //    case "DA Form 7448R":
            //        cryRpt12.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt12;
            //        break;

            //    case "DA Form 7449R":
            //        cryRpt13.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt13;
            //        break;

            //    case "DA Form 7450R":
            //        cryRpt14.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt14;
            //        break;

            //    case "DA Form 7451R":
            //        cryRpt15.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt15;
            //        break;

            //    case "DA Form 85R":
            //        cryRpt16.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt16;
            //        break;

            //    case "DA Form 88R":
            //        cryRpt17.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt17;
            //        break;

            //    case "DA Form 7304R":
            //        cryRpt18.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt18;
            //        break;

            //    case "Malfunction":
            //        cryRpt19.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt19;
            //        break;

            //    case "Firing Order Summary":
            //        cryRpt20.SetDataSource(ds);
            //        crystalReportViewer1.ReportSource = cryRpt20;
            //        break;

            //}

            ReportDocument cryRpt = new ReportDocument();
            
            sFile = tbFileName.Text;
            Console.WriteLine("sFile loaded here " + sFile);
            cryRpt.Load(sFile);
            crystalReportViewer1.ReportSource = cryRpt;

            cryRpt.SetDataSource(ds);

            ParameterFields paramFields = new ParameterFields();
            ParameterField paramField = new ParameterField();
            ParameterDiscreteValue discreteVal = new ParameterDiscreteValue();


            paramField.ParameterFieldName = "IDCode_param";


            sIDCode = sIDCodes[0];
            discreteVal.Value = sIDCode;
            paramField.CurrentValues.Add(discreteVal);

            crystalReportViewer1.ParameterFieldInfo.Clear();
            crystalReportViewer1.ParameterFieldInfo.Add(paramField);

            paramField.HasCurrentValue = true;
            crystalReportViewer1.Refresh();

            //cryRpt.PrintToPrinter(1, false, 0, 0);
            //MessageBox.Show("Pause", "Go");
            //}



        }


        private void Form1_Load(object sender, EventArgs e)
        {
            //tbLogFilePath.Text = "c:\\logfile_85.txt";
            tbLogFilePath.Text = "C:\\Documents and Settings\\ATI\\Desktop\\Reports\\Test Logs\\20111004 Oct 04 2011 Scenario 11-01-37.txt";
            tbFileName.Text = "C:\\Documents and Settings\\ATI\\My Documents\\Visual Studio 2010\\Projects\\SmartRangeReports\\SmartRangeReports\\Report_DA_Form_85R.rpt";

        }

        private string GetSubString(string sText, string sSearch, int iAdd)
        {
            String sTemp;
            int iPos, iPos2;

            iPos = sText.IndexOf(sSearch, 0);
            iPos += iAdd;
            iPos2 = sText.IndexOf(";", iPos);
            iPos2 = iPos2 - iPos;
            sTemp = sText.Substring(iPos, iPos2);
            return sTemp;
        }

        public static string Mid(string param, int startIndex, int length)
        {
            //start at the specified index in the string ang get N number of
            //characters depending on the lenght and assign it to a variable
            //string result = param.Substring(startIndex, length);
            string result = param.Substring(startIndex);
            //return the result of the operation
            return result;
        }

        private int GetStartPos(string sText, string sHeader, int iStartPos)
        {
            int ipos;
            sText = sText.ToUpper();
            ipos = sText.IndexOf(sHeader, 0);
            if (ipos < iStartPos && ipos >= 0)
                iStartPos = ipos;

            return iStartPos;
        }

        private void cbReportList_SelectedIndexChanged(object sender, EventArgs e)
        {
        }

        private void button2_Click(object sender, EventArgs e)
        {
            string sFile = "";
            
            OpenFD2.Title = "Choose a Report";
            //OpenFD.InitialDirectory = "C:";
            OpenFD2.InitialDirectory = System.Environment.GetFolderPath(Environment.SpecialFolder.Personal);
            OpenFD2.Filter = "Reports|*.rpt";
            OpenFD2.ShowDialog();

            sFile = OpenFD2.FileName;
            tbFileName.Text = sFile;
        }

        private void bLogFilePath_Click(object sender, EventArgs e)
        {
            string sFile = "";

            OpenFD.Title = "Choose a Log File";
            OpenFD.InitialDirectory = "C:";
            //OpenFD.InitialDirectory = System.Environment.GetFolderPath(Environment.SpecialFolder.Personal);
            OpenFD.Filter = "Log Files|*.txt";
            OpenFD.ShowDialog();

            sFile = OpenFD.FileName;
            tbLogFilePath.Text = sFile;
        }


    }
}
