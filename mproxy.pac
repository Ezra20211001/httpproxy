var FilterHost = {
    "www.baidu.com": true,
};

function FindProxyForURL(url, host) {
    if (FilterHost[host]) {
        return "PROXY 127.0.1:7890";
    }

   if (shExpMatch(url,"*.google.com/*")) {
     return "PROXY localhost:8081";
   }

   return "DIRECT"; 
}