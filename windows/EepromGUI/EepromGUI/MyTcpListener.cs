using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace EepromGUI
{
    class MyTcpListener
    {
        public MyTcpListener()
        {
            /*UdpClient client = new UdpClient(_localPort, AddressFamily.InterNetwork);
            IPEndPoint groupEp = new IPEndPoint(IPAddress.Broadcast, _remotePort);
            client.Connect(groupEp);

            client.Send(_findBytes, _findBytes.Length);
            client.Close();*/

            IPEndPoint recvEp = new IPEndPoint(IPAddress.Any, 4227);
            UdpClient udpResponse = new UdpClient(4227);
            for (int i = 0; i < 10; i++)
            {

                Byte[] recvBytes = udpResponse.Receive(ref recvEp);
                Console.WriteLine("Received: " + recvEp.Address);
            }
            
        }


    }
}
