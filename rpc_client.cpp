//  ZeroMQ experiment, from "Hello World client in C++"
//  Connects REQ socket to tcp://localhost:5555
//  Sends "Hello" to server, expects "World" back
#include <zmq.hpp>
#include <string>
#include <iostream>

// protobuf
#include <rpc.pb.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

//
#include <ch_frb_io.hpp>

using namespace std;
using namespace ch_frb_io;

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

        // Construct message header...
        RpcRequestHeader hdr;
        hdr.set_funcname(method->name());
        string hdrserial = hdr.SerializeAsString();

        // Message body...
        string serial = request->SerializeAsString();

        // Send multi-part message (sent as one network packet)
        socket->send(hdrserial.data(), hdrserial.size(), ZMQ_SNDMORE);
        socket->send(serial.data(), serial.size(), 0);

        // Wait for reply...
        zmq::message_t reply;
        socket->recv(&reply);
        cout << "Received result of size " << reply.size() << endl;

        // Parse reply.
        //response->ParseFromArray(reply.data(), reply.size());
        // If reply will be > 64 MB, need to do:
        pb::io::ZeroCopyInputStream* s = new pb::io::ArrayInputStream(reply.data(), reply.size());
        pb::io::CodedInputStream* cs = new pb::io::CodedInputStream(s);
        cs->SetTotalBytesLimit(512*1024*1024, 512*1024*1024);
        response->ParseFromCodedStream(cs);

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
    DummyRpcController con;
    
    //  Do 10 requests, waiting each time for a response
    for (int request_nbr = 0; request_nbr < 10; request_nbr++) {

        con.Reset();

        GetBeamMetadata_Request req;
        GetBeamMetadata_Response resp;
        
        rpcservice->GetBeamMetadata(&con, &req, &resp, NULL);

        // deep copy from pb::Map back to std::map
        map<string, uint64_t> meta(resp.metadata().begin(),
                                   resp.metadata().end());

        cout << "Main metadata:" << endl;
        for (map<string,uint64_t>::iterator it = meta.begin();
             it != meta.end(); it++) {
            cout << "  " << it->first << ": " << it->second << endl;
        }

        int nbeams = resp.beams_size();
        for (int i=0; i<nbeams; i++) {
            // deep copy pb::Maps
            map<string, uint64_t> beam(resp.beams(i).metadata().begin(),
                                       resp.beams(i).metadata().end());
            cout << "Beam " << i << "metadata:" << endl;
            for (map<string,uint64_t>::iterator it = beam.begin();
                 it != beam.end(); it++) {
                cout << "  " << it->first << ": " << it->second << endl;
            }
        }


        // Send chunk request
        con.Reset();
        GetChunks_Request creq;
        GetChunks_Response cres;

        creq.add_beam(2);
        creq.add_beam(3);
        creq.set_min_chunk(0);
        creq.set_max_chunk(0);

        rpcservice->GetChunks(&con, &creq, &cres, NULL);

        cout << "Received GetChunks response..." << endl;

        int nchunks = cres.chunks_size();
        cout << "Received " << nchunks << " chunks" << endl;
        for (int i=0; i<nchunks; i++) {
            Chunk chunk = cres.chunks(i);

            std::shared_ptr<assembled_chunk> ch = assembled_chunk::make
                (chunk.beam_id(), chunk.nupfreq(), chunk.nt_per_packet(), chunk.fpga_counts_per_sample(),
                 chunk.ichunk());
            // ch.beam_id = chunk.beam_id();
            // ch.nupfreq = chunk.nupfreq();
            // ch.nt_per_packet = chunk.nt_per_packet();
            // ch.fpga_counts_per_sample = chunk.fpga_counts_per_sample();
            // ch.nt_coarse = chunk.nt_coarse();
            // ch.nscales = chunk.nscales();
            // ch.ndata = chunk.ndata();
            // ch.ichunk = chunk.ichunk();
            // ch.isample = chunk.isample();
            // ch.scales = new 
            if (chunk.ndata() != ch->ndata) {
                cout << "Chunk ndata mismatch" << endl;
                continue;
            }
            if (chunk.nscales() != ch->nscales) {
                cout << "Chunk nscales mismatch" << endl;
                continue;
            }
            for (int j=0; j<chunk.nscales(); j++) {
                ch->scales[j] = chunk.scales(j);
                ch->offsets[j] = chunk.offsets(j);
            }
            memcpy(ch->data, chunk.data().data(), chunk.ndata());

            cout << "Unpacked chunk with " << chunk.nscales() << " scales and "
                 << chunk.ndata() << " data elements" << endl;
        }

        break;
    }
    return 0;
}
