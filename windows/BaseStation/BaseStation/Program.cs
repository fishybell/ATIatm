using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Net;
using System.Net.Sockets;

namespace BaseStation
{
    // main class just starts the work
    class Program
    {
        static void Main(string[] args)
        {
            // create a thread to do all the work
            Thread thread = new Thread(new ThreadStart(Work.DoWork));
            thread.Start();
        }
    }

    // worker class does the actual work (typical capitalist program)
    class Work
    {
        Work() { }

        public static void DoWork()
        {
            // create an IP endpoint for our broadcast packet
            int groupPort = 53530;
            IPEndPoint groupEP = new IPEndPoint(IPAddress.Parse("255.255.255.255"), groupPort);

            // create and open a socket
            Socket socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
            socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Broadcast, 1);

            // loop forever
            while (true)
            {
                // send
                socket.SendTo(System.Text.ASCIIEncoding.ASCII.GetBytes("Base Station"), groupEP);

                // sleep 10 seconds
                Thread.Sleep(10000);
            }
        }
    }
}
