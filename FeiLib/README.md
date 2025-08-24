# Configureations available   

## Logger    
LogShowLevel --- level for log to show   
LogSaveLevel --- level for log to save   
in following valuse:    
>   "trace" "debug" "info" "warn" "err" "critical" "off"

LogFile --- FileName to save log.
A valid path is acceptable   

LogFlushTime --- Time for log to be flushed    
in milliseconds.


## TCP Server    

SocketKeepAliveTime --- For socket option: SO_KEEPALIVE    
in seconds //may be    

TcpIdleTime --- For Tcp server to call OnIdleCallback     
in milliseconds.     

## SSL 
ALPNPreference --- To open http2 support.   
'http2' to enable http2 connection when it comes to alpn selection in ssl hand shake   

## Http Server   
HttpRequestWaitTime --- Time to wait for request transport finish.     
In Seconds.      

HttpConnectTimeout --- Time to wait for http connction time out.
In Seconds.        

Http2CloseImm --- Wether to close a http2 connection when connection survive time is larger than HttpRequestWaitTime, default is false, which will cause server to send GOAWAY frame first.  
"1" to enable          

Http2DrainTimeout --- Time to wait for closing connection after GOAWAY frame was sent.    
In Seconds.     


## Http2    
H2EnablePush --- To enable Http2 push promise.       
"1" to enable     

H2MaxStreamNum --- Max Stream can be processed in one http2 session.  
