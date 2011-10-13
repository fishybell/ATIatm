using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;


namespace EepromGUI
{
    class MyTcpListener
    {
        public static void TestListener()
        {
            TcpListener server = null;
            try
            {
                // Set the TcpListener on port 13000.
                Int32 port = 4422;
                IPHostEntry entry = Dns.GetHostEntry("sam5");
                String ip = entry.AddressList[0].ToString();
                //IPAddress localAddr = IPAddress.Parse(ip);
               

                // TcpListener server = new TcpListener(port);
                server = new TcpListener(IPAddress.Parse(ip), port);

                // Start listening for client requests.
                server.Start();

                // Buffer for reading data
                Byte[] bytes = new Byte[256];
                String data = null;

                // Enter the listening loop.
                while (true)
                {
                    Console.Write("Waiting for a connection... ");

                    // Perform a blocking call to accept requests.
                    // You could also user server.AcceptSocket() here.
                    TcpClient client = server.AcceptTcpClient();
                    Console.WriteLine("Connected!");

                    data = null;

                    // Get a stream object for reading and writing
                    NetworkStream stream = client.GetStream();
                    String message = "I M";
                    // Translate the passed message into ASCII and store it as a Byte array.
                    Byte[] dataSend = System.Text.Encoding.ASCII.GetBytes(message);

                    // Get a client stream for reading and writing.
                    //  Stream stream = client.GetStream();

                    // Send the message to the connected TcpServer. 
                    stream.Write(dataSend, 0, dataSend.Length);

                    Console.WriteLine("Sent: {0}", message);

                    int i;

                    // Loop to receive all the data sent by the client.
                    while ((i = stream.Read(bytes, 0, bytes.Length)) != 0)
                    {
                        // Translate data bytes to a ASCII string.
                        data = System.Text.Encoding.ASCII.GetString(bytes, 0, i);
                        Console.WriteLine("Received: {0}", data);

                        // Process the data sent by the client.
                        data = data.ToUpper();

                        byte[] msg = System.Text.Encoding.ASCII.GetBytes(data);

                        // Send back a response.
                        stream.Write(msg, 0, msg.Length);
                        Console.WriteLine("Sent: {0}", data);
                    }

                    // Shutdown and end connection
                    client.Close();
                }
            }
            catch (SocketException e)
            {
                Console.WriteLine("SocketException: {0}", e);
            }
            finally
            {
                // Stop listening for new clients.
                server.Stop();
            }


            Console.WriteLine("\nHit enter to continue...");
            Console.Read();
        }   

    }
}
