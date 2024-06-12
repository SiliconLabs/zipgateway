module("luci.controller.admin.zipgateway", package.seeall)

function index()
  local page  = node("admin", "network", "zipgateway")                    
  page.target = cbi("zipgateway/setup")
  page.title  = "Z/IP Gateway" 
  page.order  = 99
end  
