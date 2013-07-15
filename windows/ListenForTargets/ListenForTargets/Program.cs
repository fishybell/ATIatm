using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Net.Sockets;
using System.Threading;
using System.Net;

namespace ListenForTargets
{

    class Program
    {
        /***********************************************************
         * This listens for any ip adresses broadcasting on port 4227
         * *********************************************************/
        private static void ListenForBroadcast(int viewTimes)
        {
            // port 4227 is the port where the targets are broadcasting hello
            List<string> ipList = new List<string>();
            IPEndPoint recvEp = new IPEndPoint(IPAddress.Any, 4227);
            UdpClient udpResponse = new UdpClient(4227);
            int counter = 0;
            String ip = "";
            while (true)
            {
                Byte[] recvBytes = udpResponse.Receive(ref recvEp);
                String ip2 = recvEp.Address.ToString();
                //recvEp.Address is the ip adress of the machine
                if (ip != recvEp.Address.ToString() && viewTimes != 0)
                {
                    if (viewTimes > 0 && counter <= viewTimes)
                    {
                        ip = recvEp.Address.ToString();
                        counter++;
                    } else {
                        return;
                    }
                }
                if (!ipList.Contains(ip2))
                {
                    ipList.Add(ip2);
                    Console.WriteLine(ip2);
                }
                // sleep 5 seconds
                Thread.Sleep(5);
            }
        }

        static void Main(string[] args)
        {
            int ipsToSee = 0;
            // Test if input arguments were supplied: 
            if (args.Length > 0)
            {
                switch (args[0])
                {
                    case "-n":
                        try
                        {
                            bool test = int.TryParse(args[1], out ipsToSee);
                        }
                        catch (Exception)
                        {
                            Console.WriteLine("The argument value for -n is incorrect.  It must be a number.");
                            Console.WriteLine("Press <enter> to exit");
                            Console.ReadLine();
                            return;
                        }
                        break;
                    case "?":
                        Console.WriteLine("ListenForTargets Command Line Arguments\nPress <enter> to exit\n---------------------------------");
                        Console.WriteLine("-n <number>: The number of IP addresses the program sees before it quits.");
                        Console.ReadLine();
                        return;
                    default:
                        break;
                }
            }
            ListenForBroadcast(ipsToSee);
        }
    }
}
