       function showData(){
          var xmlhttp;
          if (window.XMLHttpRequest){
             xmlhttp = new XMLHttpRequest();
          } else {
             xmlhttp = new ActiveXObject("Microsoft.XMLHTTP");
          }
          xmlhttp.onreadystatechange=function(){
             if(xmlhttp.readyState==4 && xmlhttp.status==200){
             document.getElementById("message2").innerHTML="read status 200";
                document.getElementById("message1").innerHTML=xmlhttp.responseText;
             }
          }
          xmlhttp.open("GET", "/cgi-bin/getDatabaseData.cgi", true);
          xmlhttp.send();
       }
       function showBobber(){
          var xmlhttp;
          if (window.XMLHttpRequest){
             xmlhttp = new XMLHttpRequest();
          } else {
             xmlhttp = new ActiveXObject("Microsoft.XMLHTTP");
          }
          xmlhttp.onreadystatechange=function(){
             if(xmlhttp.readyState==4 && xmlhttp.status==200){
                var jsonresponse = xmlhttp.responseText;
                var obj = JSON.parse(jsonresponse);
                var ipadd = obj.IP;
                var subnet = obj.SUBNET;
                var bobhits = obj.BOBHITS;
                var minrnd = obj.MINRND;
                var maxrnd = obj.MAXRND;
                var vers = obj.VERSION;
                var sensitivity = obj.SENSE;
                document.getElementById('ipadd').value = ipadd;
                document.getElementById('subnet').value = subnet;
                document.getElementById('bobhits').value = bobhits;
                document.getElementById('sensitivity').value = sensitivity;
                document.getElementById('minrnd').value = minrnd;
                document.getElementById('maxrnd').value = maxrnd;
                document.getElementById('ativersion').innerHTML = vers;
             }
          }
          xmlhttp.open("GET", "/cgi-bin/bobberUpdate.cgi", true);
          xmlhttp.send();
       }
       function postBobber(){
          var xmlhttpp;
          if (window.XMLHttpRequest){
             xmlhttpp = new XMLHttpRequest();
          } else {
             xmlhttpp = new ActiveXObject("Microsoft.XMLHTTP");
          }
          xmlhttpp.onreadystatechange=function(){
             if(xmlhttpp.readyState==4 && xmlhttpp.status==200){
             }
          }
          var ipadd = document.getElementById("ipadd").value;
          var subnet = document.getElementById("subnet").value;
          var bobhits = document.getElementById("bobhits").value;
          var sensitivity = document.getElementById("sensitivity").value;
          var minrnd = document.getElementById("minrnd").value;
          var maxrnd = document.getElementById("maxrnd").value;
          var data = "ipadd=" + ipadd + "&" + "subnet=" + subnet + "&" + "bobhits=" + bobhits + "&" + "sensitivity=" + sensitivity + "&" + "minrnd=" + minrnd + "&" + "maxrnd=" + maxrnd;
          xmlhttpp.open("POST", "/cgi-bin/bobberUpdate.cgi", false);
          xmlhttpp.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
//          xmlhttpp.setRequestHeader("Content-length", data.length);
//          xmlhttpp.setRequestHeader("Connection", "close");
          xmlhttpp.send(data);
          setTimeout("redirectPage()", 7000);
          document.getElementById('message1').innerHTML = "You will be redirected within 7 seconds";
       }
       function expose(){
          var xmlhttp;
          if (window.XMLHttpRequest){
             xmlhttp = new XMLHttpRequest();
          } else {
             xmlhttp = new ActiveXObject("Microsoft.XMLHTTP");
          }
          xmlhttp.onreadystatechange=function(){
             if(xmlhttp.readyState==4 && xmlhttp.status==200){
             }
          }
          xmlhttp.open("GET", "/cgi-bin/expose.cgi", true);
          xmlhttp.send();
       }
       function conceal(){
          var xmlhttp;
          if (window.XMLHttpRequest){
             xmlhttp = new XMLHttpRequest();
          } else {
             xmlhttp = new ActiveXObject("Microsoft.XMLHTTP");
          }
          xmlhttp.onreadystatechange=function(){
             if(xmlhttp.readyState==4 && xmlhttp.status==200){
             }
          }
          xmlhttp.open("GET", "/cgi-bin/conceal.cgi", true);
          xmlhttp.send();
       }
       function redirectPage(){
          var ipadd = document.getElementById("ipadd").value;
          window.location = "http://" + ipadd;
       }
