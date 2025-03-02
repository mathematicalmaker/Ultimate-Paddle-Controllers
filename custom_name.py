#import pprint
Import("env")

board_config = env.BoardConfig()
board_config.update("build.usb_product", "Paddles Set 1")
board_config.update("name", "Rotary Encoder Paddles")
board_config.update("vendor", "mathematicalmaker")   
board_config.update("build.usb_description", "Test Description")
# 
'''
print("Board Configuration:")
pprint.pprint(board_config.__dict__)

print("\nEnvironment Dump:")
pprint.pprint(env.Dictionary())
'''