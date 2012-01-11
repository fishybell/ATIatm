using System;
using System.Collections.Generic;
using System.Text;
using System.Net.Sockets;
using System.Threading;
using System.ComponentModel;
using System.Collections.ObjectModel;
using System.Net;
using System.IO;

namespace EepromGUI
{
    public class Eeprom
    {
        TcpClient client;
        NetworkStream stream;
        String ip = "";
        int port = 4422;
        //instance of our delegate
        private static ProcessStatus _status;
        //our thread object
        private static Thread _thread;
        //our ISynchronizeInvoke object
        private static ISynchronizeInvoke _synch;
        //our delegate, which will be used to marshal our call to the UI thread for updating the UI
        public delegate void ProcessStatus(string Message, int status);

        /***********************************************
         * Constructor that helps set up interthread communication
         * ********************************************/
        public Eeprom(ISynchronizeInvoke syn, ProcessStatus notify)
        {
            //set the _synch & _status
            _synch = syn;
            _status = notify;
        }

        /***********************
         * Empty Constructor
         * ********************/
        public Eeprom()
        {
            _synch = null;
            _status = null;
        }

        /***************************************
         * Start a separate thread to listen for
         * incoming messages
         * ************************************/
        public void StartProcess(String machine)
        {
            IPHostEntry entry = Dns.GetHostEntry(machine);
            this.ip = entry.AddressList[0].ToString();
            _thread = new System.Threading.Thread(ListenForUpdates);
            _thread.IsBackground = true;
            _thread.Name = "Listen to TCP thread";
            _thread.Start();
        }


        /*****************************************
         * Start the TCP/IP connection to the given machine
         * ***************************************/
        public bool StartConnection(String machine)
        {
            // Create the pieces to make the TCP connection
            //Server server = new Server();
            IPHostEntry entry = Dns.GetHostEntry(machine);
            this.ip = entry.AddressList[0].ToString();
            
            //Connect to a tcp socket
            try
            {
                client = new TcpClient(ip, port);
                // Get a client stream for reading and writing.
                stream = client.GetStream();
                return true;
            }
            catch (ArgumentNullException e)
            {
                Console.WriteLine("ArgumentNullException: {0}", e);
                return false;
            }
            catch (SocketException e)
            {
                Console.WriteLine("SocketException: {0}", e);
                return false;
            }
        }

        /********************************
         * This creates a listener on a seperate 
         * thread to log incoming respones
         * ******************************/
        public void ListenForUpdates()
        {

            try
            {
                // Create a TcpClient.
                //threadClient = new TcpClient(ip, port);

                //threadStream = threadClient.GetStream();
                //otherStream.ReadTimeout = 1500;

                // Loop forever
                while (true)
                {
                    // Buffer to store the response bytes.
                    Byte[] data = new Byte[4096];

                    // String to store the response ASCII representation.
                    String responseData = String.Empty;

                    // Check to see if this NetworkStream is readable.
                    if (stream.CanRead)
                    {
                        byte[] myReadBuffer = new byte[1024];
                        StringBuilder myCompleteMessage = new StringBuilder();
                        int numberOfBytesRead = 0;

                        // Incoming message may be larger than the buffer size.
                        do
                        {
                            numberOfBytesRead = stream.Read(myReadBuffer, 0, myReadBuffer.Length);

                            myCompleteMessage.AppendFormat("{0}", Encoding.ASCII.GetString(myReadBuffer, 0, numberOfBytesRead));

                        }
                        while (stream.DataAvailable);

                        // Print out the received message to the console.
                        Console.WriteLine("Received: " + myCompleteMessage);
                        UpdateStatus(myCompleteMessage.ToString(), 0);

                    }
                    else
                    {
                        Console.WriteLine("Sorry.  You cannot read from this NetworkStream.");
                    }
                    // sleep 5 seconds
                    Thread.Sleep(5000);
                }

            }
            catch (ArgumentNullException e)
            {
                Console.WriteLine("ArgumentNullException: {0}", e);
            }
            catch (SocketException e)
            {
                Console.WriteLine("SocketException: {0}", e);
            }
            catch (IOException e)
            {
                Console.WriteLine("IOException: {0}", e);
            }
        }

        /******************************************************
         * Update the GUI
         * ***************************************************/
        private static void UpdateStatus(string msg, int status)
        {
            // create our Object Array
            object[] items = new object[2];
            //populate our array with the parameters passed to our method
            items[0] = msg;
            items[1] = status;
            //call the delegate
            _synch.Invoke(_status, items);
        }

        /******************************************
         * Send a message to the Eeprom board of the
         * target and read back its response
         * ***************************************/
        public String sendMessage(String message)
        {
            String answer = "";
            message = message + "\n";
            try
            {
                // Translate the passed message into ASCII and store it as a Byte array.
                Byte[] data = System.Text.Encoding.ASCII.GetBytes(message);

                // Send the message to the connected TcpServer. 
                if (stream.CanWrite)
                {
                    stream.Write(data, 0, data.Length);
                }
                else
                {
                    Console.WriteLine("Can't write message");
                }

                Console.WriteLine("Sent: {0}", message);

            }
            catch (ArgumentNullException e)
            {
                Console.WriteLine("ArgumentNullException: {0}", e);
            }
            catch (SocketException e)
            {
                Console.WriteLine("SocketException: {0}", e);
            }
            return answer;
        }

        /********************************
         * Closes the TCP/IP connection
         * *****************************/
        public void CloseConnection()
        {
            stream.Flush();
            stream.Close();
            client.Close();
        }

        /*******************************
         * Kills the listen thread
         * *****************************/
        public void killThread()
        {
            if (_thread.IsAlive)
            {
                _thread.Abort();
            }
        }

    }

}
