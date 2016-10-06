//  ZeroMQ experiment, from "Hello World client in C++"
//  Connects REQ socket to tcp://localhost:5555
//  Sends "Hello" to server, expects "World" back
#include <zmq.hpp>
#include <string>
#include <iostream>

// msgpack
#include <msgpack.hpp>
#include <sstream>

// protobuf
#include <rpc.pb.h>

using namespace std;

namespace pb = ::google::protobuf;

// protobuf rpc over zmq
class ZmqRpcChannel : public pb::RpcChannel {
public:
    ZmqRpcChannel(zmq::socket_t* socket) : socket(socket) {}
    virtual ~ZmqRpcChannel() {}

    void CallMethod(const pb::MethodDescriptor* method,
                    pb::RpcController* controller,
                    const pb::Message* request,
                    pb::Message* response,
                    pb::Closure* done) {
        cout << "CallMethod: " << method->name() << endl;

        RpcRequestHeader hdr;
        hdr.set_funcname(method->name());

        //int bs = request->ByteSize();
        //request->SerializeToArray(

        /*
        string hdrserial = hdr.SerializeAsString();
        string serial = request->SerializeAsString();
        //zmq::message_t request(buffer.data(), buffer.size(), NULL);

        cout << "Sending message of size " << serial.size() << " with header "
             << hdrserial.size() << endl;

        string fullmsg = hdrserial + serial;
        socket->send(fullmsg.data(), fullmsg.size(), 0);
         */

        string hdrserial = hdr.SerializeAsString();
        string serial = request->SerializeAsString();

        socket->send(hdrserial.data(), hdrserial.size(), ZMQ_SNDMORE);
        socket->send(serial.data(), serial.size(), 0);
        //zmq::message_t request(

        zmq::message_t reply;
        socket->recv(&reply);
        cout << "Received result of size " << reply.size() << endl;

        response->ParseFromArray(reply.data(), reply.size());

        cout << "Got response: " << response << endl;

    }

protected:
    zmq::socket_t* socket;
};


class DummyRpcController : public pb::RpcController {
public:
    DummyRpcController() {
        Reset();
    }

    virtual void Reset() {
        failed = false;
        errorText = "";
    }

    virtual bool Failed() const { return failed; }
    virtual string ErrorText() const { return errorText; }
    virtual void StartCancel() {}

    virtual void SetFailed(const string& reason) {
        errorText = reason;
    }
    virtual bool IsCanceled() const { return false; }
    virtual void NotifyOnCancel(pb::Closure* done) {}

protected:
    bool failed;
    string errorText;
};




int main() {
    //  Prepare our context and socket
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REQ);
    
    cout << "Connecting to Chime L1 RPC server..." << endl;
    socket.connect("tcp://localhost:5555");

    ZmqRpcChannel channel(&socket);
    L1Service* rpcservice = new L1Service::Stub(&channel);
    
    //  Do 10 requests, waiting each time for a response
    for (int request_nbr = 0; request_nbr < 10; request_nbr++) {

        // RPC request buffer.
        msgpack::sbuffer buffer;
        string funcname = "get_beam_metadata";
        //tuple<string, bool> call(funcname, false);
        //msgpack::pack(buffer, call);
        msgpack::pack(buffer, funcname);

        

        GetBeamMetadata_Request req;
        cout << "getBeamMetadata request size: " << req.ByteSizeLong() << endl;

        GetBeamMetadata_Response resp;
        
        DummyRpcController con;

        rpcservice->GetBeamMetadata(&con, &req, &resp, NULL);

        cout << "Buffer size: " << buffer.size() << endl;

        // no copy
        zmq::message_t request(buffer.data(), buffer.size(), NULL);

        cout << "Sending metadata request " << request_nbr << endl;
        socket.send(request);

        //  Get the reply.
        zmq::message_t reply;
        socket.recv(&reply);
        cout << "Received result " << request_nbr << endl;

        cout << "Reply has size " << reply.size() << endl;

        const char* reply_data = reinterpret_cast<const char *>(reply.data());

        msgpack::object_handle oh =
            msgpack::unpack(reply_data, reply.size());
        msgpack::object obj = oh.get();
        cout << obj << endl;




        // Send chunk request
        buffer = msgpack::sbuffer();
        
        funcname = "get_chunks";
        msgpack::pack(buffer, funcname);
        vector<vector<uint64_t> > args;

        vector<uint64_t> chunk;
        // beam id
        chunk.push_back(3);
        // chunk id?
        chunk.push_back(1);
        args.push_back(chunk);
                        
        msgpack::pack(buffer, args);

        cout << "Buffer size: " << buffer.size() << endl;

        // no copy
        request = zmq::message_t(buffer.data(), buffer.size(), NULL);

        cout << "Sending chunk request " << request_nbr << endl;
        socket.send(request);

        //  Get the reply.
        socket.recv(&reply);
        cout << "Received result " << request_nbr << endl;

        cout << "Reply has size " << reply.size() << endl;

        reply_data = reinterpret_cast<const char *>(reply.data());

        oh = msgpack::unpack(reply_data, reply.size());
            
        obj = oh.get();
        cout << obj << endl;


    }
    return 0;
}
