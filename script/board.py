#!/usr/bin/python3
# board.py

# python official modules
import os
import time
import termios
import array
from fcntl import ioctl
from subprocess import call

def search():
    global __mode
    global __busy
    global __dev_file
    __mode = None
    __busy = None
    __dev_file = None

    for s in os.listdir('/sys/class/scsi_disk'):
        c = s.split(':')[0]
        scan_file = '/sys/class/scsi_host/host' + c + '/scan'
        try:
            with open(scan_file, 'w') as f:
                f.write('- - -')
        except:
            print('Fail opening ' + scan_file)
    time.sleep(2)

    found = False
    for s in os.listdir('/sys/class/scsi_disk'):
        model_file = '/sys/class/scsi_disk/' + s + '/device/model'
        try:
            with open(model_file, 'r') as f:
                model = f.read().splitlines()[0]
        except:
            print('Fail opening ' + model_file)

        if model == 'YATAPDONG BAREFO':
            __mode = 0
            found = True
        elif model == 'OpenSSD Jasmine ':   # note the last space character
            __mode = 1
            found = True

        if found:
            busy_file = '/sys/class/scsi_disk/' + s + '/device/device_busy'
            try:
                with open(busy_file, 'r') as f:
                    __busy = bool(int(f.read().rstrip('\n')))
            except:
                print('Fail opening ' + busy_file)
            try:
                blk = os.listdir('/sys/class/scsi_disk/' + s + '/device/block')
                if blk.__len__() == 0:
                    print('No block devices found')
                elif blk.__len__() > 1:
                    print('Multiple devices found')
                else:
                    __dev_file = '/dev/' + blk[0]
            except:
                raise Exception('Fail listing block devices')
            mode()
            busy()
            dev_file()
            return __dev_file

    #raise Exception('Fail finding Jasmine board')
    print('Fail finding Jasmine board')

#def rescan():
#    if __rescan_file is not None:
#        with open(__rescan_file, 'w') as f:
#            f.write('- - -')
#           time.sleep(2)

def mode():
    if __mode is not None:
        if __mode == 0:
            print('Factory')
        else:
            print('Normal')
    else:
        print('Unknown')

def busy():
    if __busy is not None:
        if __busy:
            print('Busy')
        else:
            print('Free')
    else:
        print('Unknown')

def dev_file():
    if __dev_file is not None:
        print(__dev_file)
    else:
        print('Unknown')

def factory():
    ctrl = termios.TIOCMBIC
    port = array.array('i', [termios.TIOCM_RTS])
    __ctrl_relay(ctrl, port)

    reset()

def normal():
    ctrl = termios.TIOCMBIS
    port = array.array('i', [termios.TIOCM_RTS])
    __ctrl_relay(ctrl, port)

    reset()

def reset():
    ctrl = termios.TIOCMBIS
    port = array.array('i', [termios.TIOCM_DTR])
    __ctrl_relay(ctrl, port)

    ctrl = termios.TIOCMBIC
    __ctrl_relay(ctrl, port)

def download():
    path = os.path.dirname(__file__) + '/../installer'
    cwd = os.getcwd()
    #print('[debug]\tchange cwd to ' + path)
    os.chdir(path)
    call(['./installer', __dev_file, '0'])
    os.chdir(cwd)
    #print('[debug]\tchange cwd to ' + cwd)

def badblk():
    path = os.path.dirname(__file__) + '/../installer'
    cwd = os.getcwd()
    #print('[debug]\tchange cwd to ' + path)
    os.chdir(path)
    call(['./installer', __dev_file, '1'])
    os.chdir(cwd)
    #print('[debug]\tchange cwd to ' + cwd)

def relay(path):
    global __relay_fd
    try:
        __relay_fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    except:
        print('Cannot open relay file: ' + path)

# internal functions
def __ctrl_relay(ctrl, port):
    if __relay_fd is not None:
        try:
            ioctl(__relay_fd, ctrl, port, 1)
            time.sleep(2)
        except:
            print('Cannot control relay')
    else:
        print('Please set relay file first')

__mode = None
__busy = None
__dev_file = None
__relay_fd = None
search()
