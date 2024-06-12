
m = Map("zipgateway", "Z/IP Gateway")


s = m:section(TypedSection, "zipgateway", "") 
s.anonymous = true 


o = s:option(Flag, "enable", translate("Enable Z/IP Gateway")) 
o.rmempty = false                                                           
                                                                                            
function o.cfgvalue(self)                                                   
  return luci.sys.init.enabled("zipgateway")                             
       and self.enabled or self.disabled                                                   
end                                                                         
                                                                                            
function o.write(self, section, value)                                                              
    if value == self.enabled then                                       
        luci.sys.init.enable("zipgateway")                             
        luci.sys.call("env -i /etc/init.d/zipgateway start >/dev/null")
    else                                                                
        luci.sys.call("env -i /etc/init.d/zipgateway stop >/dev/null") 
        luci.sys.init.disable("zipgateway")                            
    end                                                                 
end     

device = s:option(ListValue, "SerialAPIPortName", "Serial port", "the serialport to where the USB Z-Wave SerialAP dongle is inserted.") 

local device_suggestions = nixio.fs.glob("/dev/tty*S*")

if device_suggestions then
        local node
        for node in device_suggestions do
                device:value(node)
        end
end

device_suggestions = nixio.fs.glob("/dev/ttyACM*")                                                     
if device_suggestions then                                                                                                                  
        local node                                                                                                                          
        for node in device_suggestions do                                                                                                   
                device:value(node)                                                                                                          
        end                                                                                                                                 
end 
                                        

s:option(Value, "PanIp6", "HAN prefix","The prefix of the Z-Wave network(HAN)")
panpre = s:option(Value, "PanIp6PrefixLength", "HAN prefix len","Length of PAN prefix.")
panpre.size = 2

s:option(Value, "UnsolicitedDestinationIp6", "Unsolicited destination","The destination IPv6 address of unsolicited Z-Wave packages.")
s:option(Value, "UnsolicitedDestinationPort", "Unsolicited destination port","The destination port address of unsolicited Z-Wave packages.")
s:option(Value, "LanIp6", "LAN IPv6 addr","Address of the Z/IP gateway application.")

lanpre = s:option(Value, "LanIp6PrefixLength", "LAN prefix len","Length of the LAN prefix.")
lanpre.size = 2

s:option(Value, "LanGw6", "LAN gateway", "Gateway should mostlikely be set to then ip6addr of the OpenWRT lan interface ")

s:option(Flag, "Ip4Disable", "Disable IPv4", "")

s:option(Value, "Portal", "Portal hostname", "Name of portal to connect to.")
s:option(Value, "CaCert", "CA Certificate","Certificate file ie. the public certificate of the portal provider.")
s:option(Value, "Cert", "Public certificate.", "Public certificate of the gateway")
s:option(Value, "PrivKey", "Private key", "Private key of the gateway")
s:option(Value, "PSK", "PreShared keyi","Preshared key used in DTLS")


return m
