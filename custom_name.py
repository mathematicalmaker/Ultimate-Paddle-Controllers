Import("env")

board_config = env.BoardConfig()
board_config.update("build.usb_product", "Set1 - Paddles") #USB Product name
board_config.update("build.hwids", [
  ["0x1209", "0x0001"], #Generic VID/PID
  ["0x2886", "0x002F"]  #Orignal values for Seeeduino XIAO 2nd pair
])
