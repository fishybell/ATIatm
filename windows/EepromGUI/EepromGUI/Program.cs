using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

namespace EepromGUI
{
    static class Program
    {
        /// <summary>
        /// The main entry point for the application.
        /// </summary>
        [STAThread]

        static void Main()
        {
            //MyTcpListener listener = new MyTcpListener();
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Form1());
        }
    }
}

