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
            string sText, sLineText, sTemp;
            int iPos, iLen, iElement;
            int iStartPos;
            string sIDCode, exp;
            bool bOut;
            List<string> logLines = new List<string>();
            List<string> shooterGroups = new List<string>();
            //textBox1.Text = "c:\\dummy.txt";
            sPath = tbLogFilePath.Text;

            StreamReader streamReader = new StreamReader(sPath);
            sText = streamReader.ReadToEnd();
            streamReader.Close();

            //string[] sIDCodes = new string[];
            //string[] sIDCodes = new string[];
            List<string> sIDCodes = new List<string>();
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
            DataTable dt11 = ds.GROUP;
            //DataTable dt12 = ds.TABLE;
            //DataTable dt13 = ds.TASK;

            DataRow shooter_row;
            DataRow hit_row;
            DataRow task_start_row;
            DataRow task_end_row;
            DataRow target_row;
            DataRow spotter_row;
            DataRow malfunction_row;
            DataRow scenario_start_row;
            DataRow target_exposed_row;
            DataRow target_concealed_row;
            DataRow group_row;
            //DataRow table_row;
            //DataRow task_row;

            DataRow[] foundRows;

            // Initialize new rows
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

                iStartPos = GetStartPos(sText, "TABLE:", iStartPos);

                iStartPos = GetStartPos(sText, "TASK:", iStartPos);

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
                    logLines.Add(sLineText);
                }
            }

            //Pull out relevant lines of the log file to add to the database

            // Add SCENARIOSTART
            foreach (var line in logLines)
            {
                if (line.Contains("SCENARIOSTART:"))
                {
                    scenario_start_row = dt8.NewRow();

                    String[] scenStartSplit = splitLine(line);
                    foreach (var item in scenStartSplit)
                    {
                        if (item.IndexOf("TIMESTAMP(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            scenario_start_row["TIMESTAMP"] = addTag(item);
                        }
                        else if (item.IndexOf("NAME(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            scenario_start_row["NAME"] = addTag(item);
                        }
                    }
                    dt8.Rows.Add(scenario_start_row);
                }
            }

            // Add SHOOTER
            foreach (var line in logLines)
            {
                if (line.Contains("SHOOTER:"))
                {
                    shooter_row = dt1.NewRow();
                    spotter_row = dt6.NewRow();

                    String[] shooterSplit = splitLine(line);
                    String shooterType = "";
                    //If the shooterType is shooter add additional shooter attributes
                    if (line.IndexOf("TYPE(Shooter)", StringComparison.OrdinalIgnoreCase) >= 0)
                    {
                        shooterType = "shooter";
                    }
                    else if ((line.IndexOf("TYPE(Spotter)", StringComparison.OrdinalIgnoreCase) >= 0))
                    {
                        shooterType = "spotter";
                    }
                    foreach (var item in shooterSplit)
                    {
                        if (shooterType == "shooter" || shooterType == "")
                        {
                            // Look for indexes while ignoring case and add them to the data tables
                            if (item.IndexOf("FIRSTNAME(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooter_row["FIRSTNAME"] = addTag(item);
                            }
                            else if (item.IndexOf("LASTNAME(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooter_row["LASTNAME"] = addTag(item);
                            }
                            else if (item.IndexOf("GROUP(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooterGroups.Add(addTag(item));
                                shooter_row["GROUP"] = addTag(item);
                            }
                            else if (item.IndexOf("UNIT(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooter_row["UNIT"] = addTag(item);
                            }
                            else if (item.IndexOf("GROUP(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooter_row["GROUP"] = addTag(item);
                            }
                            else if (item.IndexOf("IDCODE(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooter_row["IDCode"] = addTag(item);
                                sIDCodes.Add(addTag(item)); // store this for opening multiple reports
                            }
                            else if (item.IndexOf("MIDDLE(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooter_row["MIDDLE"] = addTag(item);
                            }
                            else if (item.IndexOf("RANK(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooter_row["RANK"] = addTag(item);
                            }
                            else if (item.IndexOf("TIMESTAMP(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooter_row["DATE"] = addTag(item);
                            }
                            else if (item.IndexOf("SSN(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooter_row["SSN"] = addTag(item);
                            }
                            else if (item.IndexOf("ORDER(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooter_row["ORDER"] = addTag(item);
                            }
                            else if (item.IndexOf("LANE(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                shooter_row["LANE"] = addTag(item);
                            }
                        }
                        else if (shooterType == "spotter")
                        {
                            // Look for indexes while ignoring case and add them to the data tables
                            if (item.IndexOf("FIRSTNAME(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                spotter_row["FIRSTNAME"] = addTag(item);
                            }
                            else if (item.IndexOf("LASTNAME(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                spotter_row["LASTNAME"] = addTag(item);
                            }
                            else if (item.IndexOf("GROUP(", StringComparison.OrdinalIgnoreCase) >= 0)
                            {
                                spotter_row["GROUP"] = addTag(item);
                            }
                        }
                    }
                    // Add to the appropriate table if 
                    if (shooterType == "spotter")
                    {
                        dt6.Rows.Add(spotter_row);
                    }
                    else if (shooterType == "shooter" || shooterType == "")
                    {
                        dt1.Rows.Add(shooter_row);
                    }
                }
            }

            // Add TARGET
            foreach (var line in logLines)
            {
                if (line.Contains("TARGET:"))
                {
                    target_row = dt5.NewRow();

                    String[] targetSplit = splitLine(line);
                    String thisTargetName = "";
                    String[] groups = { "", "" };
                    String sGroup = "";
                    foreach (var item in targetSplit)
                    {
                        if (item.IndexOf("RANGE(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            target_row["RANGE"] = addTag(item);
                        }
                        else if (item.IndexOf("NAME(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            thisTargetName = addTag(item);
                            target_row["NAME"] = thisTargetName;
                        }
                        else if (item.IndexOf("GROUP(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            groups = addTag(item).Split(' ');
                            foreach (var piece in groups)
                            {
                                if (shooterGroups.Contains(piece))
                                {
                                    sGroup = piece;
                                }
                            }
                        }
                    }
                    // Only add a new row if this target hasn't been added yet
                    if (!dt5.Rows.Contains(thisTargetName))
                    {
                        dt5.Rows.Add(target_row);
                    }
                    // add multiple groups
                    for (int i = 0; i < groups.Length; i++)
                    {
                        if (sGroup != groups[i])
                        {
                            group_row = dt11.NewRow();
                            group_row["TARGET_NAME"] = thisTargetName;
                            group_row["SHOOTER_GROUP"] = sGroup;
                            group_row["OTHER_GROUP"] = groups[i];

                            dt11.Rows.Add(group_row);
                        }
                    }
                }
            }

            // Add TargetConcealed
            foreach (var line in logLines)
            {
                if (line.Contains("TARGETCONCEALED:"))
                {
                    target_concealed_row = dt10.NewRow();

                    String[] tarConSplit = splitLine(line);
                    foreach (var item in tarConSplit)
                    {
                        if (item.IndexOf("NAME(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            target_concealed_row["NAME"] = addTag(item);
                        }
                        if (item.IndexOf("TIMESTAMP(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            target_concealed_row["TIMESTAMP"] = addTag(item);
                        }
                        if (item.IndexOf("KILL(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            target_concealed_row["KILL"] = addTag(item);
                        }
                    }
                    dt10.Rows.Add(target_concealed_row);
                }
            }

            // Add TASKSTART
             for (int i = 0; i < logLines.Count; i++)
            {
                String hitLine = "";
                if (logLines.ElementAt(i).Contains("TASKSTART:"))
                {
                    task_start_row = dt3.NewRow();
                    //table_row = dt12.NewRow();
                    //task_row = dt13.NewRow();
                    

                    String[] taskStartSplit = splitLine(logLines.ElementAt(i));
                    String sTable = "";
                    String sRow = "";
                    String sRound = "";
                    String sTask = "";
                    foreach (var item in taskStartSplit)
                    {
                        if (item.IndexOf("TABLE(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            sTable = addTag(item);
                            //table_row["TABLE_ID"] = sTable;
                            task_start_row["TABLE"] = sTable;
                            //task_row["TABLE_ID"] = sTable;
                        }
                        if (item.IndexOf("TASK(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            sTask = addTag(item);
                            task_start_row["TASK"] = sTask;
                            //task_row["TASK_ID"] = sTask;
                        }
                        if (item.IndexOf("ROW(", StringComparison.OrdinalIgnoreCase) >= 0)  // Range needs to be changed to Row
                        {
                            sRow = addTag(item);
                            task_start_row["ROW"] = sRow;
                            //task_row["ROW"] = sRow;
                        }
                        if (item.IndexOf("ROUND(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            sRound = addTag(item);
                            task_start_row["ROUND"] = sRound;
                            //task_row["ROUND"] = sRound;
                        }
                        if (item.IndexOf("TIMESTAMP(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            task_start_row["TIMESTAMP"] = addTag(item);
                            //task_row["TIMESTAMP"] = addTag(item);
                        }
                    }
                    //Make sure that there are no duplicate rows already in the datatable
                    //  If there are, then they must be deleted from the table.
                    exp = "TABLE = '" + sTable + "' AND TASK = '" + sTask + "' AND ROW = '" + sRow + "' AND ROUND = '" + sRound + "'";
                    foundRows = dt3.Select(exp);
                    //int i = foundRows[0].ToString;
                    if (foundRows.Length > 0)
                        foundRows[0].Delete();

                    dt3.Rows.Add(task_start_row);
                    //dt13.Rows.Add(task_row);

                    /*int j = 0;
                    while (!logLines.ElementAt(i + j).Contains("TASKEND:"))
                    {
                        if (logLines.ElementAt(i + j).Contains("HIT:"))
                        {
                            hit_row = dt2.NewRow();
                            hitLine = logLines.ElementAt(i + j);
                            // add hit count here
                            String[] hitTaskSplit = splitLine(hitLine);
                            foreach (var item in hitTaskSplit)
                            {
                                if (item.IndexOf("NAME(", StringComparison.OrdinalIgnoreCase) >= 0)
                                {
                                    hit_row["NAME"] = addTag(item);
                                }
                                else if (item.IndexOf("TIMESTAMP(", StringComparison.OrdinalIgnoreCase) >= 0)
                                {
                                    hit_row["TIMESTAMP"] = addTag(item);
                                }
                                // Increase hit count of this table/task

                            }
                            hit_row["TASK_ID"] = sTask;
                            hit_row["TABLE_ID"] = sTable;
                            hit_row["ROW"] = sRow;
                            // Increase hit counter of this table/task/row
                            string expression = "TASK_ID = " + sTable + " and TABLE_ID = " + sTask;
                            DataRow[] selectedRows = dt13.Select(expression);
                            if (selectedRows.Count() > 0)
                            {
                                int counter = (int)selectedRows[0]["HIT_COUNT"];
                                counter++;
                                selectedRows[0]["HIT_COUNT"] = counter;
                            }
                            dt2.Rows.Add(hit_row);
                        }
                        j++;
                    }*/
                }
            }

            // Add TargetExposed
            foreach (var line in logLines)
            {
                if (line.Contains("TARGETEXPOSED:"))
                {
                    target_exposed_row = dt9.NewRow();

                    String[] tarExpSplit = splitLine(line);
                    foreach (var item in tarExpSplit)
                    {
                        if (item.IndexOf("NAME(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            target_exposed_row["NAME"] = addTag(item);
                        }
                        if (item.IndexOf("TIMESTAMP(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            target_exposed_row["TIMESTAMP"] = addTag(item);
                        }
                        if (item.IndexOf("ATTRITION(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            target_exposed_row["ATTRITION"] = addTag(item);
                        }
                    }
                    dt9.Rows.Add(target_exposed_row);
                }
            }


            // Add TASKEND
            /*for (int i = 0; i < logLines.Count; i++)
            {
                String hitLine = "";
                if (logLines.ElementAt(i).Contains("TASKEND:"))
                {
                    task_end_row = dt4.NewRow();
                    int j = 0;
                    while (!logLines.ElementAt(i - j).Contains("TASKSTART:"))
                    {
                        if (logLines.ElementAt(i - j).Contains("HIT:"))
                        {
                            hitLine = logLines.ElementAt(i - j);
                            // add hit count here
                            String[] hitTaskSplit = splitLine(hitLine);
                            foreach (var item in hitTaskSplit)
                            {
                                if (item.IndexOf("NAME(", StringComparison.OrdinalIgnoreCase) >= 0)
                                {
                                    task_end_row["HIT_NAME"] = addTag(item);
                                }
                            }
                        }
                        j++;
                    }
                    String[] taskEndSplit = splitLine(logLines.ElementAt(i));
                    String tTable = "";
                    String tRange = "";
                    String tRound = "";
                    String tTask = "";
                    foreach (var item in taskEndSplit)
                    {
                        if (item.IndexOf("TABLE(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            tTable = addTag(item);
                            task_end_row["TABLE"] = tTable;
                        }
                        if (item.IndexOf("TASK(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            tTask = addTag(item);
                            task_end_row["TASK"] = tTask;
                        }
                        if (item.IndexOf("ROW(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            tRange = addTag(item);
                            task_end_row["ROW"] = tRange;
                        }
                        if (item.IndexOf("ROUND(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            tRound = addTag(item);
                            task_end_row["ROUND"] = tRound;
                        }
                        if (item.IndexOf("TIMESTAMP(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            task_end_row["TIMESTAMP"] = addTag(item);
                        }
                    }

                    //Make sure that there are no duplicate rows already in the datatable
                    //  If there are, then they must be deleted from the table.
                    exp = "TABLE = '" + tTable + "' AND TASK = '" + tTask + "' AND ROW = '" + tRange + "' AND ROUND = '" + tRound + "'";
                    foundRows = dt4.Select(exp);
                    //int i = foundRows[0].ToString;
                    if (foundRows.Length > 0)
                        foundRows[0].Delete();

                    dt4.Rows.Add(task_end_row);
                }
                
            }*/

            foreach (var line in logLines)
            {
                if (line.Contains("TASKEND:"))
                {
                    task_end_row = dt4.NewRow();

                    String[] taskEndSplit = splitLine(line);
                    String tTable = "";
                    String tRange = "";
                    String tRound = "";
                    String tTask = "";
                    foreach (var item in taskEndSplit)
                    {
                        if (item.IndexOf("TABLE(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            tTable = addTag(item);
                            task_end_row["TABLE"] = tTable;
                        }
                        if (item.IndexOf("TASK(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            tTask = addTag(item);
                            task_end_row["TASK"] = tTask;
                        }
                        if (item.IndexOf("ROW(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            tRange = addTag(item);
                            task_end_row["ROW"] = tRange;
                        }
                        if (item.IndexOf("ROUND(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            tRound = addTag(item);
                            task_end_row["ROUND"] = tRound;
                        }
                        if (item.IndexOf("TIMESTAMP(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            task_end_row["TIMESTAMP"] = addTag(item);
                        }
                    }

                    //Make sure that there are no duplicate rows already in the datatable
                    //  If there are, then they must be deleted from the table.
                    exp = "TABLE = '" + tTable + "' AND TASK = '" + tTask + "' AND ROW = '" + tRange + "' AND ROUND = '" + tRound + "'";
                    foundRows = dt4.Select(exp);
                    //int i = foundRows[0].ToString;
                    if (foundRows.Length > 0)
                        foundRows[0].Delete();

                    dt4.Rows.Add(task_end_row);
                }
            }

            // Add HIT
            foreach (var line in logLines)
            {
                if (line.Contains("HIT:"))
                {
                    hit_row = dt2.NewRow();

                    String[] hitSplit = splitLine(line);
                    foreach (var item in hitSplit)
                    {
                        if (item.IndexOf("NAME(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            hit_row["NAME"] = addTag(item);
                        }
                        if (item.IndexOf("TIMESTAMP(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            // Find table and task according to the timestamp.
                            DateTime hitTime = ConvertDateTime(addTag(item));
                            //string expression = "1=1";
                            //DataRow[] selectedRows = dt3.Select(expression);  //dt3 - taskstart, dt4 - taskend
                            foreach (var row in dt3.Rows)
                            {
                                DateTime taskStartTime = ConvertDateTime(((WindowsFormsApplication1.DataSet1.TASKSTARTRow)(row)).TIMESTAMP);
                                String taskStartTable = ((WindowsFormsApplication1.DataSet1.TASKSTARTRow)(row)).TABLE;
                                String taskStartTask = ((WindowsFormsApplication1.DataSet1.TASKSTARTRow)(row)).TASK;
                                String taskExpression = "TABLE = '" + taskStartTable + "' AND TASK = '" + taskStartTask + "'";
                                DataRow[] taskEndRows = dt4.Select(taskExpression);
                                DateTime taskEndTime = ConvertDateTime(((WindowsFormsApplication1.DataSet1.TASKENDRow)(taskEndRows[0])).TIMESTAMP);
                                // Find the taskstart and taskend objects that the hit timestamp falls between
                                if (hitTime > taskStartTime && hitTime < taskEndTime)
                                {
                                    hit_row["TABLE_ID"] = taskStartTable;
                                    hit_row["TASK_ID"] = taskStartTask;
                                }
                            }
                            hit_row["TIMESTAMP"] = hitTime;
                        }
                        /*if (item.IndexOf("TABLE(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            hit_row["TABLE_ID"] = addTag(item);
                        }
                        if (item.IndexOf("TASK(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            hit_row["TASK_ID"] = addTag(item);
                        }*/
                    }
                    dt2.Rows.Add(hit_row);
                }
            }

            // Add MALFUNCTION
            foreach (var line in logLines)
            {
                if (line.Contains("MALFUNCTION:"))
                {
                    malfunction_row = dt7.NewRow();

                    String[] malSplit = splitLine(line);
                    foreach (var item in malSplit)
                    {
                        if (item.IndexOf("ADDRESS(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            malfunction_row["ADDRESS"] = addTag(item);
                        }
                        if (item.IndexOf("TIMESTAMP(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            malfunction_row["TIMESTAMP"] = addTag(item);
                        }
                        if (item.IndexOf("DEFINITION(", StringComparison.OrdinalIgnoreCase) >= 0)
                        {
                            malfunction_row["DEFINITION"] = addTag(item);
                        }
                    }
                    dt7.Rows.Add(malfunction_row);
                }
            }

            //Test data
            /*for (int i = 1; i < 6; i++)
            {
                table_row = dt12.NewRow();
                table_row["TABLE"] = 1;
                table_row["TASK"] = i.ToString();
                table_row["ROW"] = "Row" + i;
                if (i % 2 == 0)
                {
                    table_row["HITS"] = "X";
                }
                table_row["ROUND"] = "None";
                dt12.Rows.Add(table_row);
            }

            for (int i = 1; i < 6; i++)
            {
                table_row = dt12.NewRow();
                table_row["TABLE"] = 2;
                table_row["TASK"] = i.ToString();
                table_row["ROW"] = "Row" + i;
                if (i % 3 == 0)
                {
                    table_row["HITS"] = "X";
                }
                table_row["ROUND"] = "None";
                dt12.Rows.Add(table_row);
            }*/
            

            ReportDocument cryRpt = new ReportDocument();

            sFile = tbFileName.Text;
            Console.WriteLine("sFile loaded here " + sFile);
            cryRpt.Load(sFile);
            crystalReportViewer1.ReportSource = cryRpt;

            cryRpt.SetDataSource(ds);

            /*ParameterFields paramFields = new ParameterFields();
            ParameterField paramField = new ParameterField();
            ParameterDiscreteValue discreteVal = new ParameterDiscreteValue();

            paramField.ParameterFieldName = "IDCode_param";

            // Run multiple reports
            for (int i = 0; i < sIDCodes.Count; i++)
            {
                sIDCode = sIDCodes.ElementAt(i);
                //sIDCode = sIDCodes[i];
                discreteVal.Value = sIDCode;
                paramField.CurrentValues.Add(discreteVal);

                crystalReportViewer1.ParameterFieldInfo.Clear();
                crystalReportViewer1.ParameterFieldInfo.Add(paramField);

                paramField.HasCurrentValue = true;*/
                crystalReportViewer1.Refresh();
            //}

            //cryRpt.PrintToPrinter(1, false, 0, 0);
            //MessageBox.Show("Pause", "Go");
            //}



        }

        // Converts the log timestamp to a datetime data type
        private DateTime ConvertDateTime(string dateTime)
        {
            int year = Convert.ToInt32(dateTime.Substring(0, 4));
            int month = Convert.ToInt32(dateTime.Substring(5, 2));
            int day = Convert.ToInt32(dateTime.Substring(8, 2));
            int hour = Convert.ToInt32(dateTime.Substring(11, 2));
            int minute = Convert.ToInt32(dateTime.Substring(14, 2));
            int second = Convert.ToInt32(dateTime.Substring(17, 2));
            DateTime newtime = new DateTime(year, month, day, hour, minute, second);
            return newtime;
        }


        private void Form1_Load(object sender, EventArgs e)
        {
            tbLogFilePath.Text = "C:\\Users\\Public\\Documents\\Log Files\\Reports Info\\Scenarios\\85-1.txt";
            //tbLogFilePath.Text = "\\\\tao\\shellyb\\Shelly's VS Projects\\Reports Info\\Scenarios\\88-12.txt";
            //tbLogFilePath.Text = "C:\\Users\\Chris\\Desktop\\Log Files\\TargetLogFiles\\85-1.txt";
            tbFileName.Text = "C:\\Users\\ATI\\Documents\\Visual Studio 2010\\Projects\\SmartRangeReports\\85_Table.rpt";

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

        /***************************************************
         * Gets the string piece from between the parenthesis
         * *************************************************/
        private String addTag(String splitString)
        {
            int index1 = splitString.IndexOf('(');
            int index2 = splitString.LastIndexOf(')');
            String piece = splitString.Substring(index1 + 1, (index2 - index1) - 1);
            return piece;
        }

        /***************************************************
         * Splits the string to parse into nice components
         * that contain the tag name and parenthesis.
         * *************************************************/
        private String[] splitLine(String inputLine)
        {
            String[] inputSplit = inputLine.Split(' ');
            for (int i = 0; i < inputSplit.Length; i++)
            {
                int count = 1;
                if (inputSplit[i] != "" && !inputSplit[i].EndsWith("),") && !inputSplit[i].EndsWith(")") && !inputSplit[i].EndsWith(":"))
                {
                    while (!inputSplit[i].Contains(")"))
                    {
                        inputSplit[i] = inputSplit[i] + " " + inputSplit[i + count];
                        inputSplit[i + count] = "";
                        count++;
                    }
                    i = i + count-1;
                }
            }
            return inputSplit;
        }

        private void testButton_Click(object sender, EventArgs e)
        {
            Console.WriteLine("Page number: " + crystalReportViewer1.GetCurrentPageNumber());
        }
    }
}
