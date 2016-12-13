from __future__ import print_function
import zmq
import msgpack
import threading

def client_thread(context, me):
    socket = context.socket(zmq.DEALER)
    socket.connect('tcp://localhost:5555')
    
    beams = [0,1,2]
    minchunk = 0
    maxchunk = 5
    filename_pat = 'chunk-%02llu-chunk%08llu-py.msgpack'
    msg = (msgpack.packb('write_chunks') +
           msgpack.packb([beams, minchunk, maxchunk, filename_pat]))
    print('Client', me, ': sending request...')
    socket.send(msg)
    #print('Client', me', : Waiting for write_chunks replies...')
    while True:
        msg = socket.recv()
        #print('Client', me, ': Received reply: %i bytes' % len(msg))
        # print('Message:', repr(msg))
        rep = msgpack.unpackb(msg)
        print('Client', me, ': got reply:', rep)
    socket.close()

if __name__ == '__main__':
    context = zmq.Context()

    t1 = threading.Thread(target=client_thread, args=(context,1))
    t2 = threading.Thread(target=client_thread, args=(context,2))
    t1.start()
    t2.start()

    t1.join()
    t2.join()
    context.term()
    