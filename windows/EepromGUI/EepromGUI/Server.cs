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
    public class Server
    {
        TcpClient client;
        TcpClient threadClient;
        NetworkStream stream;
        NetworkStream threadStream;
        String ip = "";
        int port = 4422;
        //instance of our delegate
        private static ProcessStatus _status;
        //our thread object
        private static Thread _thread;
        //our ISynchronizeInvoke object
        private static ISynchronizeInvoke _synch;
        //our list we'll be adding to
        private static Collection<string> _listItems = new Collection<string>();
        //our delegate, which will be used to marshal our call to the UI thread for updating the UI
        public delegate void ProcessStatus(string Message, int status);

        public Server(ISynchronizeInvoke syn, ProcessStatus notify)
        {
            //set the _synch & _status
            _synch = syn;
            _status = notify;
        }

        public Server()
        {
            _synch = null;
            _status = null;
        }

        public void StartProcess(String machine)
        {
            IPHostEntry entry = Dns.GetHostEntry(machine);
            this.ip = entry.AddressList[0].ToString();
            _thread = new System.Threading.Thread(ListenForUpdates);
            _thread.IsBackground = true;
            _thread.Name = "Listen to TCP thread";
            _thread.Start();
        }

        public void StartConnection(String machine)
        {
            // Create the pieces to make the TCP connection
            Console.WriteLine("Called StartConnection");
            Server server = new Server();
            IPHostEntry entry = Dns.GetHostEntry(machine);
            this.ip = entry.AddressList[0].ToString();
            
            //Connect to a tcp socket
            try
            {
                client = new TcpClient(ip, port);
                // Get a client stream for reading and writing.
                stream = client.GetStream();
                
            }
            catch (ArgumentNullException e)
            {
                Console.WriteLine("ArgumentNullException: {0}", e);
            }
            catch (SocketException e)
            {
                Console.WriteLine("SocketException: {0}", e);
            }
        }

        /********************************
         * This creates a listener on a seperate thread to log incoming respones
         * ******************************/
        public void ListenForUpdates()
        {

            try
            {
                // Create a TcpClient.
                threadClient = new TcpClient(ip, port);

                threadStream = threadClient.GetStream();
                //otherStream.ReadTimeout = 1500;

                // Loop forever
                while (true)
                {
                    // Buffer to store the response bytes.
                    Byte[] data = new Byte[4096];

                    // String to store the response ASCII representation.
                    String responseData = String.Empty;

                    // Check to see if this NetworkStream is readable.
                    if (threadStream.CanRead)
                    {
                        byte[] myReadBuffer = new byte[1024];
                        StringBuilder myCompleteMessage = new StringBuilder();
                        int numberOfBytesRead = 0;

                        // Incoming message may be larger than the buffer size.
                        do
                        {
                            numberOfBytesRead = threadStream.Read(myReadBuffer, 0, myReadBuffer.Length);

                            myCompleteMessage.AppendFormat("{0}", Encoding.ASCII.GetString(myReadBuffer, 0, numberOfBytesRead));

                        }
                        while (threadStream.DataAvailable);

                        // Print out the received message to the console.
                        Console.WriteLine("Thread message: " + myCompleteMessage);
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
        }

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

                // Receive the TcpServer.response.

                // Buffer to store the response bytes.
                data = new Byte[4096];

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
                    Console.WriteLine("You received the following message : " +
                                                 myCompleteMessage);
                    answer = myCompleteMessage.ToString();
                }
                else
                {
                    Console.WriteLine("Sorry.  You cannot read from this NetworkStream.");
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
            return answer;
        }

        public void CloseConnection()
        {
            stream.Flush();
            stream.Close();
            client.Close();
        }

        public static String ConnectEeprom(String message, String machine)
        {
            Server server = new Server();

            TcpClient client = new TcpClient();

            IPHostEntry entry = Dns.GetHostEntry(machine);
            String ip = entry.AddressList[0].ToString();
            //IPEndPoint serverEndPoint = new IPEndPoint(IPAddress.Parse(ip), 4422);
     
            return Connect(ip, message + "\n");

        }

        static String Connect(String server, String message)
        {
            String answer = "";
            try
            {
                // Create a TcpClient.
                // Note, for this client to work you need to have a TcpServer 
                // connected to the same address as specified by the server, port
                // combination.
                Int32 port = 4422;
                TcpClient client = new TcpClient(server, port);

                // Translate the passed message into ASCII and store it as a Byte array.
                Byte[] data = System.Text.Encoding.ASCII.GetBytes(message);

                // Get a client stream for reading and writing.
                //  Stream stream = client.GetStream();

                NetworkStream stream = client.GetStream();

                //stream.Flush();
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

                // Receive the TcpServer.response.

                // Buffer to store the response bytes.
                data = new Byte[4096];

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
                    Console.WriteLine("You received the following message : " +
                                                 myCompleteMessage);
                    answer = myCompleteMessage.ToString();
                }
                else
                {
                    Console.WriteLine("Sorry.  You cannot read from this NetworkStream.");
                }

                // Close everything.
                stream.Flush();
                stream.Close();
                client.Close();
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

        // Listen for messages and log them
        public static String Listen(String machine)
        {
            Console.WriteLine("Start listen");
            Server server = new Server();

            IPHostEntry entry = Dns.GetHostEntry(machine);
            String ip = entry.AddressList[0].ToString();
            String answer = "";
            try
            {
                // Create a TcpClient.
                // Note, for this client to work you need to have a TcpServer 
                // connected to the same address as specified by the server, port
                // combination.
                Int32 port = 4422;
                TcpClient client = new TcpClient(ip, port);

                // Get a client stream for reading and writing.
                //  Stream stream = client.GetStream();

                NetworkStream stream = client.GetStream();
                stream.ReadTimeout = 1500;

                // Receive the TcpServer.response.

                // Buffer to store the response bytes.
                Byte[] data = new Byte[4096];

                // String to store the response ASCII representation.
                String responseData = String.Empty;

                // Check to see if this NetworkStream is readable.
                if (stream.CanRead)
                {
                    //if (stream.DataAvailable)
                    //{
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
                    Console.WriteLine(myCompleteMessage);
                    answer = myCompleteMessage.ToString();
                    //}
                }
                else
                {
                    Console.WriteLine("Sorry.  You cannot read from this NetworkStream.");
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
            /*catch (IOException e)
            {
               
            }*/
 
            return answer;
        }

    }

    class Work
    {
        String ip = "";
        int port = 0;
        NetworkStream stream;
        public Work(String ip, int port) 
        {
            this.ip = ip;
            this.port = port;
            //this.stream = stream;
        }

        public void DoWork()
        {
            Server server = new Server();
            //IPHostEntry entry = Dns.GetHostEntry(machine);
            //String ip = entry.AddressList[0].ToString();
            String answer = "";
            try
            {
                // Create a TcpClient.
                // Note, for this client to work you need to have a TcpServer 
                // connected to the same address as specified by the server, port
                // combination.
                TcpClient client = new TcpClient(ip, port);

                NetworkStream stream = client.GetStream();
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
                        //if (stream.DataAvailable)
                        //{
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
                        Console.WriteLine("Thread message: " + myCompleteMessage);
                        //UpdateStatus(myCompleteMessage.ToString(), 0);
                        //form.logData(myCompleteMessage.ToString());
                        answer = myCompleteMessage.ToString();
                        //}
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
        }
    }
}
