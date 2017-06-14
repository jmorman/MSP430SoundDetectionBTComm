import bluetooth
import sys
import re
import matplotlib.pyplot as plt

bd_addr = "00:06:66:D0:E6:2F"

port = 1

sock=bluetooth.BluetoothSocket( bluetooth.RFCOMM )
sock.connect((bd_addr, port))

sock.send("hello!!")
print("sent")

data = ""

plotvals32 = [[],[]]
plotvals128 = [[],[]]
plotvals512 = [[],[]]
plotvals2048 = [[],[]]

plt.ion()
legend_plotted = False
while(True):
  segment = sock.recv(1024).decode("utf-8")
  data += segment

  if(segment[-1:] == '\n'):
    sys.stdout.write(data)

    values = re.split('[^\d]+', data)
    values = [int(value) for value in values if value != '']
    print(values)

    data = ""

#Input format: "T sec, window W: V" -> [T, W, V]
    if(values[1] == 32):
      plotvals32[0].append(values[0])
      plotvals32[1].append(values[2]/32)
    if(values[1] == 128):
      plotvals128[0].append(values[0])
      plotvals128[1].append(values[2]/128)
    if(values[1] == 512):
      plotvals512[0].append(values[0])
      plotvals512[1].append(values[2]/512)
    if(values[1] == 2048):
      plotvals2048[0].append(values[0])
      plotvals2048[1].append(values[2]/2048)

    plt.xlabel('Time (s)')
    plt.ylabel('Scaled ADC value')

    plt.semilogy(plotvals32[0], plotvals32[1], marker='o', ms=4, lw=1, linestyle=':', color='b', label='32 window')
    plt.semilogy(plotvals128[0], plotvals128[1], marker='o', ms=4, lw=1, linestyle=':', color='r', label='128 window')
    plt.semilogy(plotvals512[0], plotvals512[1], marker='o', ms=4, lw=1, linestyle=':', color='y', label='512 window')
    plt.semilogy(plotvals2048[0], plotvals2048[1], marker='o', ms=4, lw=1, linestyle=':', color='g', label='2048 window')
    if(not legend_plotted):
      plt.legend()
      legend_plotted = True
    plt.pause(0.05)

sock.close()
