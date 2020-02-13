import wx
import sys
import os
import serial
import socket
import websocket
from websocket import create_connection
import intelhex
from intelhex import IntelHex
import time

com_ports = ["0"]
opened = False
baud_rate = ["300", "600", "1200", "2400", "4800", "9600", "14400", "19200", "28800", "38400", "57600", "115200"]
baud_sel = []
connected = False
line = ""
read_length = 512
file_name = ""
ih = IntelHex()

############################################################################################################################################


def com_list():
    global com_ports
    com_ports.clear()
    com_ports = ["0"]
    ports = ['COM%s' % (i + 1) for i in range(256)]
    for port in ports:
        try:
            s = serial.Serial(port)
            s.close()
            if com_ports[0] == "0":
                com_ports[0] = port
            else:
                com_ports.append(port)
        except(OSError, serial.SerialException):
            pass

############################################################################################################################################
############################################################################################################################################


class FileDrop(wx.FileDropTarget):

    def __init__(self, window):
        wx.FileDropTarget.__init__(self)
        self.window = window

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def OnDropFiles(self, x, y, filenames):
        try:
            global ih
            with open(filenames[0], 'r') as hexfile:
                print(filenames[0])
                ih = {0}
                ih = IntelHex(hexfile)
                index = filenames[0].rfind('\\', 0, -1)
                print(index)
                self.window.file_d.SetValue("{}".format(filenames[0][index + 1:]))

        except IOError as error:
            msg = "Unable to open File\n {}".format(str(error))
            dlg = wx.MessageDialog(None, msg, "Error", wx.OK | wx.ICON_WARNING)
            dlg.ShowModal()
            return False

        except UnicodeDecodeError as error:
            msg = "Invalid Hex file, Cannot Decode Non-Unicode type files\n {}".format(str(error))
            dlg = wx.MessageDialog(None, msg, "Error", wx.OK | wx.ICON_WARNING)
            dlg.ShowModal()
            hexfile.close()
            return False

        except intelhex.HexRecordError as error:
            msg = "Invalid Hex file\n {}".format(str(error))
            dlg = wx.MessageDialog(None, msg, "Error", wx.OK | wx.ICON_WARNING)
            dlg.ShowModal()
            hexfile.close()
            return False

        finally:
            hexfile.close()

        return True

############################################################################################################################################
############################################################################################################################################


class WindowClass(wx.Frame):

    def __init__(self, parent, title):
        super(WindowClass, self).__init__(parent)
        self.init_ui(title)

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def init_ui(self, title):

        self.menu_bar = wx.MenuBar()
        self.pnl = wx.Panel(self)

        wx.StaticBox(self.pnl, label='Socket Connection', pos=(10, 5), size=(160, 85))
        self.soc_text = wx.TextCtrl(self.pnl, pos=(20, 25), size=(140, -1))
        self.soc_text.ChangeValue("192.168.4.1:1339")
        self.soc_btn = wx.Button(self.pnl, label='Connect', pos=(40, 55), size=(100, -1))
        self.soc_btn.Bind(wx.EVT_BUTTON, self.soc_conn)

        wx.StaticBox(self.pnl, label='Serial Connection', pos=(10, 95), size=(160, 85))
        self.com_cb = wx.ComboBox(self.pnl, pos=(20, 115), size=(140, -1), choices=com_ports, style=wx.CB_READONLY)
        self.com_cb.SetSelection(0)
        self.com_btn = wx.Button(self.pnl, label='Connect', pos=(40, 145), size=(100, -1))
        self.com_btn.Bind(wx.EVT_BUTTON, self.com_conn)
        self.com_check(self)

        wx.StaticBox(self.pnl, label='Functions', pos=(10, 185), size=(160, 210))
        self.browse_btn = wx.Button(self.pnl, label='Browse File', pos=(40, 210), size=(100, -1))
        self.browse_btn.Bind(wx.EVT_BUTTON, self.on_browse)
        self.write_btn = wx.Button(self.pnl, label='Write to Device', pos=(40, 255), size=(100, -1))
        self.write_btn.Bind(wx.EVT_BUTTON, self.on_write)
        self.read_btn = wx.Button(self.pnl, label='Read Device', pos=(40, 305), size=(100, -1))
        self.read_btn.Bind(wx.EVT_BUTTON, self.on_read)
        self.erase_btn = wx.Button(self.pnl, label='Erase Device', pos=(40, 355), size=(100, -1))
        self.erase_btn.Bind(wx.EVT_BUTTON, self.on_erase)
        self.chk_func(self)

        self.reply_text = wx.TextCtrl(self.pnl, pos=(190, 12), size=(490, 400), style=wx.TE_READONLY | wx.TE_MULTILINE)
        self.reply_text.SetBackgroundColour((255, 255, 255))
        self.file_drop_target = FileDrop(self)
        self.reply_text.SetDropTarget(self.file_drop_target)

        self.file_btn = wx.Menu()
        self.baud_item = wx.Menu()
        self.refresh_item = wx.MenuItem(self.file_btn, wx.ID_ANY, "&Refresh COM", kind=wx.ITEM_NORMAL)
        self.file_btn.Append(self.refresh_item)
        for i in range(12):
            baud_sel.append(wx.MenuItem(self.baud_item, i, baud_rate[i], kind=wx.ITEM_RADIO))
            self.baud_item.Append(baud_sel[i])
        self.baud_item.Check(11, True)
        # self.file_btn.Append(wx.ID_ANY, '&Baud Rate', self.baud_item)
        self.file_btn.AppendSubMenu(self.baud_item, '&Baud Rate')
        self.read_len_item = wx.MenuItem(self.file_btn, wx.ID_ANY, "&Read Length", kind=wx.ITEM_NORMAL)
        self.file_btn.Append(self.read_len_item)
        self.exit_item = self.file_btn.Append(wx.ID_EXIT, '&Exit')
        self.menu_bar.Append(self.file_btn, '&Options')
        self.SetMenuBar(self.menu_bar)
        self.Bind(wx.EVT_MENU, self.on_close, self.exit_item)
        self.Bind(wx.EVT_MENU, self.com_check, self.refresh_item)
        self.Bind(wx.EVT_MENU, self.read_len_func, self.read_len_item)

        self.file_d = wx.TextCtrl(self.pnl, pos=(10, 400), size=(160, -1), style=wx.BORDER_NONE | wx.TE_READONLY | wx.TE_LEFT)
        self.file_d.SetValue(file_name)

        self.SetIcon(wx.Icon(self.resource_path("icon.ico")))

        self.SetSize(720, 480)
        self.SetTitle(title)
        self.SetWindowStyle(style=wx.MINIMIZE_BOX | wx.CLOSE_BOX | wx.CAPTION)
        self.Center()
        self.Show()

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def resource_path(self, relative):
        base_path = getattr(sys, '_MEIPASS', os.path.dirname(os.path.abspath(__file__)))
        return os.path.join(base_path, relative)

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def read_len_func(self, e):
        global read_length
        dlg = wx.TextEntryDialog(self.pnl, 'Enter the Length of Memory to Read (in Bytes)', 'Read Length')
        dlg.SetValue(str(read_length))
        if dlg.ShowModal() == wx.ID_OK:
            read_length = int(dlg.GetValue())
            print('You entered: %d\n' % read_length)
        dlg.Destroy()

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def com_check(self, e):
        com_list()
        self.com_cb.Clear()
        self.com_cb.Append(com_ports)
        self.com_cb.SetSelection(0)
        if com_ports[0] == "0":
            self.com_cb.Disable()
            self.com_btn.Disable()
        else:
            self.com_cb.Enable()
            self.com_btn.Enable()

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def com_conn(self, e):
        global opened
        baud = 11
        for items in self.baud_item.GetMenuItems():
            if items.IsChecked():
                baud = items.GetId()
        if not opened:
            global ser
            try:
                self.ser = serial.Serial(com_ports[self.com_cb.GetSelection()], baud_rate[baud], timeout=8)
                opened = True
                self.com_cb.Disable()
                self.com_btn.SetLabel("Disconnect")
                self.soc_btn.Disable()
                self.soc_text.Disable()
                self.chk_func(self)
                self.reply_text.SetValue("Connected to " + com_ports[self.com_cb.GetSelection()])
                self.ser.write("proge".encode())
                recv = self.ser.readline()
                if not recv.decode().find("ok"):
                    self.reply_text.AppendText("\nConnected to Microcontroller")
                elif not recv.decode().find("err"):
                    self.reply_text.AppendText("\nUnable to connect to Microcontroller")

            except(OSError, serial.SerialException):
                self.com_check(self)
        else:
            self.ser.write("disc".encode())
            self.ser.close()
            opened = False
            self.reply_text.Clear()
            self.com_btn.SetLabel("Connect")
            self.com_cb.Enable()
            self.soc_btn.Enable()
            self.soc_text.Enable()
            self.chk_func(self)

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def soc_conn(self, e):
        global connected, line
        self.reply_text.Clear()

        if not connected:
            try:
                uri = "ws://" + self.soc_text.GetValue()
                self.ws = create_connection(uri, timeout=2, sockopt=((socket.IPPROTO_TCP, socket.TCP_NODELAY, 1),))
                print(uri)
                connected = True
                self.soc_text.Disable()
                self.soc_btn.SetLabel("Disconnect")
                self.com_cb.Disable()
                self.com_btn.Disable()
                self.reply_text.SetValue("Connected to Websocket")
                line = self.ws.recv()
                if line == "error_conn":
                    self.reply_text.AppendText("\nError Connecting to Microcontroller")
                    connected = False
                    self.soc_text.Enable()
                    self.soc_btn.SetLabel("Connect")
                    self.com_check(self)
                    self.chk_func(self)
                    self.ws.close()
                elif line == "conn":
                    self.reply_text.AppendText("\nConnected to Microcontroller")
                    self.chk_func(self)

            except (socket.timeout, websocket._exceptions.WebSocketTimeoutException, OSError) as error:
                self.reply_text.SetValue("Error Connecting to WebSocket\n {}".format(str(error)))

            except ValueError:
                dlg = wx.MessageDialog(None, "Unauthorised Server", "Error", wx.OK | wx.ICON_WARNING)
                dlg.ShowModal()

        else:
            connected = False
            self.soc_text.Enable()
            self.soc_btn.SetLabel("Connect")
            self.com_check(self)
            self.chk_func(self)
            self.ws.close()

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def on_browse(self, e):
        global file_name, ih
        with wx.FileDialog(self, "Open HEX file", wildcard="HEX files (*.hex)|*.hex",
                           style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST) as fileDialog:

            if fileDialog.ShowModal() == wx.ID_CANCEL:
                return

            pathname = fileDialog.GetPath()
            try:
                with open(pathname, 'r') as self.hexfile:
                    ih = {0}
                    ih = IntelHex(self.hexfile)
                    file_name = fileDialog.GetFilename()
                    self.file_d.SetValue(file_name)

            except IOError as error:
                msg = "Unable to open File\n {}".format(str(error))
                dlg = wx.MessageDialog(None, msg, "Error", wx.OK | wx.ICON_WARNING)
                dlg.ShowModal()

            except UnicodeDecodeError as error:
                msg = "Invalid Hex file, Cannot Decode Non-Unicode type files\n {}".format(str(error))
                dlg = wx.MessageDialog(None, msg, "Error", wx.OK | wx.ICON_WARNING)
                dlg.ShowModal()

            except intelhex.HexRecordError as error:
                msg = "Invalid Hex file\n {}".format(str(error))
                dlg = wx.MessageDialog(None, msg, "Error", wx.OK | wx.ICON_WARNING)
                dlg.ShowModal()

            finally:
                self.hexfile.close()

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def send(self, data):
        self.ws.send(data)
        recv = self.ws.recv()
        return recv

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def on_write(self, e):
        try:
            length = len(ih)
            self.reply_text.AppendText("\nWriting to Device....\n")
            if connected and not opened:
                for i in range(length):
                    if i % 8 == 0:
                        self.reply_text.AppendText("\n")
                    self.reply_text.AppendText("0x{:02x}\t".format(ih[i]))
                    # print("0x{:02x}".format(self.ih[i]))
                    reply = self.send("w:" + str(ih[i]) + ":" + str(i))
                    print(reply)
                    if reply.find("ok"):
                        self.reply_text.AppendText("\n\nWriting Failed\n")
                        return
            elif opened and not connected:
                for i in range(int(length/1024) + 1):
                    dat = "w" + str(i) + ":"
                    if length - (1024 * i) < 1024:
                        for x in range(length - (1024 * i)):
                            dat += ("%02x" % ih[x + (1024 * i)])
                    elif length > (1024 * i):
                        for x in range(1024):
                            dat += ("%02x" % ih[x + (1024 * i)])
                    self.ser.write(dat.encode())
                    reply = self.ser.readline().decode()
                    print(reply)
                    j = 0
                    for i in range(int(len(dat)/2) - 1):
                        self.reply_text.AppendText("0x%c%c\t" % (dat[j+3], dat[j+4]))
                        j += 2
            self.reply_text.AppendText("\n\nWriting finished Successfully\n")
        except AttributeError as error:
            msg = "Error writing {}".format(str(error))
            dlg = wx.MessageDialog(None, msg, wx.OK | wx.ICON_WARNING)
            dlg.ShowModal()

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def on_read(self, e):
        global read_length, connected, opened
        self.reply_text.AppendText("\nReading Device....\n")
        try:
            if connected and not opened:
                for i in range(read_length):
                    if i % 8 == 0:
                        self.reply_text.AppendText("\n")
                    reply = self.send("r:" + str(i))
                    print(reply)
                    self.reply_text.AppendText("0x%02x \t" % int(reply))
            elif opened and not connected:
                dat = "r:" + str(read_length)
                self.ser.write(dat.encode())
                reply = self.ser.read(8192).decode()
                j = 0
                for i in range(int(len(reply)/2) - 1):
                    if i % 8 == 0:
                       self.reply_text.AppendText("\n")
                    self.reply_text.AppendText("0x%c%c\t" % (reply[j], reply[j+1]))
                    j += 2

                if read_length > 4096:
                    reply2 = self.ser.read(8192).decode()
                    reply2 = reply[1:]
                    j = 0
                    for i in range(int(len(reply2)/2) - 1):
                        if i % 8 == 0:
                            self.reply_text.AppendText("\n")
                        self.reply_text.AppendText("0x%c%c\t" % (reply2[j], reply2[j+1]))
                        j += 2

            self.reply_text.AppendText("\n\nReading finished\n")
        except (TypeError, ValueError):
            dlg = wx.MessageDialog(None, "Unable to Read Device", "Error", wx.OK | wx.ICON_WARNING)
            dlg.ShowModal()

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def on_erase(self, e):
        self.ser.write("erase".encode())
        reply = self.ser.readline().decode()
        dialog = wx.MessageDialog(self.pnl, "Erase Complete", 'Done', wx.OK | wx.ICON_INFORMATION)
        dialog.ShowModal()
        dialog.Destroy()


# -----------------------------------------------------------------------------------------------------------------------------------------#

    def chk_func(self, e):
        global opened, connected
        if not opened and not connected:
            self.write_btn.Disable()
            self.read_btn.Disable()
            self.erase_btn.Disable()
        else:
            self.write_btn.Enable()
            self.read_btn.Enable()
            self.erase_btn.Enable()

# -----------------------------------------------------------------------------------------------------------------------------------------#

    def on_close(self, e):
        self.Close()

############################################################################################################################################
############################################################################################################################################


def main():
    app = wx.App()
    com_list()
    main_window = WindowClass(None, title="ESP ISP")
    app.MainLoop()

############################################################################################################################################
############################################################################################################################################


if __name__ == '__main__':
    main()

############################################################################################################################################
