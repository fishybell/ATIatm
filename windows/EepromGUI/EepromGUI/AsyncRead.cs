using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Net;
using System.Net.Sockets;
using System.Threading;

namespace EepromGUI
{
    class AsyncRead
    {
        private IPAddress[] addresses;
	    private int port;
	    private WaitHandle addressesSet;
	    private TcpClient tcpClient;
	    private int failedConnectionCount;
	 
	    /// <summary>
	    /// Construct a new client from a known IP Address
	    /// </summary>
	    /// <param name="address">The IP Address of the server</param>
	    /// <param name="port">The port of the server</param>
        public AsyncRead(IPAddress address, int port)
	    {
            addresses[0] = address;
            if (port < 0)
                throw new ArgumentException();
            this.port = port;
            this.tcpClient = new TcpClient();
            this.Encoding = Encoding.Default;
	    }
	 

	 
	    /// <summary>
	    /// Construct a new client where the address or host name of
	    /// the server is known.
	    /// </summary>
	    /// <param name="hostNameOrAddress">The host name or address of the server</param>
	    /// <param name="port">The port of the server</param>
        public AsyncRead(string hostNameOrAddress, int port)
	    {
            addressesSet = new AutoResetEvent(false);
	        Dns.BeginGetHostAddresses(hostNameOrAddress, GetHostAddressesCallback, null);
            if (port < 0)
                throw new ArgumentException();
            this.port = port;
            this.tcpClient = new TcpClient();
            this.Encoding = Encoding.Default;
	    }
	 
	    /// <summary>
	    /// Private constuctor called by other constuctors
	    /// for common operations.
	    /// </summary>
	    /// <param name="port"></param>
        private AsyncRead(int port)
	    {
	        if (port < 0)
	            throw new ArgumentException();
	        this.port = port;
	        this.tcpClient = new TcpClient();
	        this.Encoding = Encoding.Default;
	    }
	 
	    /// <summary>
	    /// The endoding used to encode/decode string when sending and receiving.
	    /// </summary>
	    public Encoding Encoding { get; set; }
	 
	    /// <summary>
	    /// Attempts to connect to one of the specified IP Addresses
	    /// </summary>
	    public void Connect()
	    {
	        if (addressesSet != null)
	            //Wait for the addresses value to be set
	            addressesSet.WaitOne();
	        //Set the failed connection count to 0
	        Interlocked.Exchange(ref failedConnectionCount, 0);
	        //Start the async connect operation
	        tcpClient.BeginConnect(addresses, port, ConnectCallback, null);
	    }
	 
	    /// <summary>
	    /// Writes a string to the network using the defualt encoding.
	    /// </summary>
	    /// <param name="data">The string to write</param>
	    /// <returns>A WaitHandle that can be used to detect
	    /// when the write operation has completed.</returns>
	    public void Write(string data)
	    {
	        byte[] bytes = Encoding.GetBytes(data);
	        Write(bytes);
	    }
	 
	    /// <summary>
	    /// Writes an array of bytes to the network.
	    /// </summary>
	    /// <param name="bytes">The array to write</param>
	    /// <returns>A WaitHandle that can be used to detect
	    /// when the write operation has completed.</returns>
	    public void Write(byte[] bytes)
	    {
	        NetworkStream networkStream = tcpClient.GetStream();
	        //Start async write operation
	        networkStream.BeginWrite(bytes, 0, bytes.Length, WriteCallback, null);
	    }

        public void Read()
        {
            //We are connected successfully.
            NetworkStream networkStream = tcpClient.GetStream();
            byte[] buffer = new byte[tcpClient.ReceiveBufferSize];
            //Now we are connected start asyn read operation.
            networkStream.BeginRead(buffer, 0, buffer.Length, ReadCallback, buffer);
        }
	 
	    /// <summary>
	    /// Callback for Write operation
	    /// </summary>
	    /// <param name="result">The AsyncResult object</param>
	    private void WriteCallback(IAsyncResult result)
	    {
	        NetworkStream networkStream = tcpClient.GetStream();
	        networkStream.EndWrite(result);
	    }
	 
	    /// <summary>
	    /// Callback for Connect operation
	    /// </summary>
	    /// <param name="result">The AsyncResult object</param>
	    private void ConnectCallback(IAsyncResult result)
	    {
	        try
	        {
	            tcpClient.EndConnect(result);
	        }
	        catch
	        {
	            //Increment the failed connection count in a thread safe way
	            Interlocked.Increment(ref failedConnectionCount);
	            if (failedConnectionCount >= addresses.Length)
	            {
	                //We have failed to connect to all the IP Addresses
	                //connection has failed overall.
	                return;
	            }
	        }
	 
	        //We are connected successfully.
	        NetworkStream networkStream = tcpClient.GetStream();
	        byte[] buffer = new byte[tcpClient.ReceiveBufferSize];
	        //Now we are connected start asyn read operation.
	        networkStream.BeginRead(buffer, 0, buffer.Length, ReadCallback, buffer);
	    }
	 
	    /// <summary>
	    /// Callback for Read operation
	    /// </summary>
	    /// <param name="result">The AsyncResult object</param>
	    private void ReadCallback(IAsyncResult result)
	    {
	        int read;
	        NetworkStream networkStream;
	        try
	        {
	            networkStream = tcpClient.GetStream();
	            read = networkStream.EndRead(result);
	        }
	        catch
	        {
	            //An error has occured when reading
	            return;
            }
	 
	        if (read == 0)
	        {
	            //The connection has been closed.
	            return;
	        }
	 
	        byte[] buffer = result.AsyncState as byte[];
	        string data = this.Encoding.GetString(buffer, 0, read);
            Console.WriteLine(data);
	        //Do something with the data object here.
	        //Then start reading from the network again.
	        networkStream.BeginRead(buffer, 0, buffer.Length, ReadCallback, buffer);
	    }
 
	    /// <summary>
	    /// Callback for Get Host Addresses operation
	    /// </summary>
	    /// <param name="result">The AsyncResult object</param>
	    private void GetHostAddressesCallback(IAsyncResult result)
	    {
	        addresses = Dns.EndGetHostAddresses(result);
	        //Signal the addresses are now set
	        ((AutoResetEvent)addressesSet).Set();
	    }
    }
}
